#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# gen_doc_smoke.py —— 用 test/data 下的**语义文档**生成 example 里的类冒烟测试
#
# 思路：文档（test/data/commands/*.md、functions/*.md）已记载每条命令/函数的
#      签名、用法示例、可用上下文与副作用，据此可以合成「带正确参数的调用」，
#      比只写裸命令名有意义得多，且不需要读 C# 源码。
#
# 产出：
#   test/data/doc_smoke.tsv          每条的调用清单（可人工复核，也供生成器消费）
#   test/example/ERB/35_DOC_SMOKE.ERB  文档语义冒烟组（分组 35）
#
# 模式（mode 列）：
#   call       直接调用（参数取自文档，纯计算或可安全重复）
#   setup      需要前置状态，snippet 里已含前置语句（多行用 \n 连接）
#   skip       不宜自动执行（交互输入 / 结束程序 / 写盘 / 依赖音频·GUI 等），
#              在 ERB 里以注释行保留，注明原因
#
# 用法：
#   python3 test/gen_doc_smoke.py            # 生成 tsv 与 ERB
#   python3 test/gen_doc_smoke.py --check    # 只打印统计
# ---------------------------------------------------------------------------
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "test/data"
DOC_DIRS = {"func": DATA / "functions", "cmd": DATA / "commands"}
OUT_TSV = DATA / "doc_smoke.draft.tsv"   # 机械草稿（勿与复核清单混淆）
REVIEWED_TSV = DATA / "doc_smoke.tsv"    # 复核后的清单：ERB 以此为唯一输入
OUT_ERB = ROOT / "test/example/ERB/35_DOC_SMOKE.ERB"

ZERO_WIDTH = dict.fromkeys(map(ord, "\u200b\u200c\u200d\ufeff"), None)

# 类型占位符 → 示例实参（尽量挑在 example 游戏环境里一定合法的东西）
ARG_OF = {
    "数值表达式": "1", "数值": "1", "整数": "1",
    "字符串表达式": '"a"', "字符串": '"a"',
    "FORM格式文本": '"a"', "FORM格式字符串": '"a"', "FORM格式字符串表达式": '"a"',
    "变量名": "FLAG", "变量": "CVTMP",
    "数值型变量": "CVTMP", "字符串型变量": "CVTMP_S",
    "数值变量": "CVTMP", "字符串变量": "CVTMP_S",
    "目标变量": "CVTMP", "数组变量": "CVTMP", "一维数组": "CVTMP",
    "角色变量": "CFLAG", "目标角色": "TARGET",
    "文本": '"a"', "行数": "1", "式": "1", "关键字": "TITLE", "时间": "1",
}
# 函数参数类型 → 示例实参
FUNC_ARG_OF = {"int": "1", "str": '"a"', "long": "1", "float": "1"}

# 文档里出现这些词 → 判定为不宜自动执行（附原因）
SKIP_PATTERNS = [
    (r"等待输入|等待按键|输入等待|挂起等待|会等待|需按键|等待点击", "需要交互输入"),
    (r"结束程序|退出程序|强制退出|重启|重新启动", "会结束/重启程序"),
    (r"删除存档|写盘|写入文件|保存存档|覆盖存档|删除文件", "会写盘或改存档"),
    (r"需要音频|需要声音|依赖音频|播放声音|播放 BGM|停止 BGM|音频后端", "依赖音频后端"),
    (r"需要鼠标|鼠标按钮|需鼠标", "需要鼠标输入"),
    (r"需要文本框控件|需要 GUI|图形界面|工具提示需", "依赖 GUI 控件"),
    (r"需要网络", "需要网络"),
    (r"本仓库无实现|未注册|已禁用|未实现该命令|NotImpl", "本仓库无实现"),
    (r"无限循环", "可能陷入无限循环"),
]


def read(p: pathlib.Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace").translate(ZERO_WIDTH)


def section(text: str, title: str) -> str:
    """取 `## <title>` 一节的内容（到下一个 `## ` 为止）。"""
    m = re.search(rf"^##\s*{re.escape(title)}\s*$(.*?)(?=^##\s|\Z)", text, re.S | re.M)
    return m.group(1) if m else ""


def signatures(text: str) -> list[str]:
    body = section(text, "签名")
    out = []
    for line in body.splitlines():
        line = re.sub(r"^[-*\s]+", "", line).strip().strip("`").strip()
        if line:
            out.append(line)
    return out


def first_example(text: str) -> str | None:
    """`## 用法` 里第一个 erb 代码块的第一条语句（跳过注释/空行）。"""
    for block in re.findall(r"```erb\n(.*?)```", section(text, "用法"), re.S):
        for line in block.splitlines():
            s = line.strip()
            if not s or s.startswith(";"):
                continue
            return s
    return None


def func_call(name: str, text: str) -> str | None:
    m = re.search(rf"\b(?:int|str|long|float)\s+{re.escape(name)}\s*\(([^)]*)\)", text)
    if not m:
        return None
    args = []
    for raw in m.group(1).split(","):
        p = raw.strip()
        if not p:
            continue
        if p.endswith("..."):          # 可变参数：补两个示例实参
            parts = p[:-3].split()
            base = parts[-1] if parts else "int"
            args += [FUNC_ARG_OF.get(base, "1")] * 2
            continue
        p = p.split("=")[0].strip()
        typ = p.split()[0] if p.split() else "int"
        args.append(FUNC_ARG_OF.get(typ, "1"))
    return f"CVTMP = {name}({', '.join(args)})"


def cmd_call(name: str, text: str) -> str | None:
    """优先用文档示例首句；否则按签名的必填占位符合成。"""
    ex = first_example(text)
    if ex:
        head = re.split(r"[\s,]", ex, 1)[0]
        if head == name:
            return ex.split(";")[0].rstrip()
    ss = signatures(text)
    if not ss:
        return None
    sig = ss[0]
    if "<" not in sig:                 # 无参命令
        return name
    # 只取第一组必填占位符（{} 内为可选，先不要）
    mand = re.split(r"\{", sig)[0]
    args = []
    for ph in re.findall(r"<([^>]+)>", mand):
        key = ph.strip()
        args.append(ARG_OF.get(key, "1"))
    if not args:
        return name
    return f"{name} {', '.join(args)}"


def skip_reason(text: str) -> str | None:
    body = section(text, "语义") + section(text, "备注")
    for pat, why in SKIP_PATTERNS:
        if re.search(pat, body):
            return why
    return None


def load_reviewed():
    """复核后的清单（6 列：name kind mode snippet note [input]）。"""
    if not REVIEWED_TSV.exists():
        return None
    rows = []
    for line in REVIEWED_TSV.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        c = line.split("\t")
        c += [""] * (6 - len(c))
        rows.append(tuple(c[:6]))          # name kind mode snippet note input
    return rows


def build_draft():
    rows = []
    for kind, d in DOC_DIRS.items():
        for p in sorted(d.glob("*.md")):
            name = p.stem
            t = read(p)
            call = func_call(name, t) if kind == "func" else cmd_call(name, t)
            why = skip_reason(t)
            if why:
                mode = "skip"
            elif call is None:
                mode, why = "skip", "文档未给出可合成的调用"
            else:
                mode = "call"
            rows.append((name, kind, mode, call or "", why or "", ""))
    return rows


def main() -> int:
    draft = build_draft()
    OUT_TSV.write_text(
        "# name\tkind\tmode\tsnippet\tnote\tinput\n"
        + "\n".join("\t".join(r) for r in draft) + "\n", encoding="utf-8")

    rows = load_reviewed()
    if rows is None:
        print(f"[!] 未见复核清单 {REVIEWED_TSV.name}，先按草稿生成（生成后应经复核再回写）")
        rows = draft
        # 复核清单缺失时，草稿即当前清单
        REVIEWED_TSV.write_text(
            "# name\tkind\tmode\tsnippet\tnote\tinput\n"
            + "\n".join("\t".join(r) for r in rows) + "\n", encoding="utf-8")
    if "--check" in sys.argv:
        from collections import Counter
        print(dict(Counter((r[1], r[2]) for r in rows)))
        return 0

    funcs = [r for r in rows if r[1] == "func"]
    cmds = [r for r in rows if r[1] == "cmd"]
    body = [
        ";=============================================================",
        "; [35] 文档语义冒烟（由 test/gen_doc_smoke.py 从 test/data/*.md 生成，勿手改）",
        ";",
        "; 依据：test/data/commands/*.md、test/data/functions/*.md —— 每条命令/函数的",
        ";       签名、用法示例与副作用描述；参数取自文档，不再一律传 0 或写裸命令名。",
        "; 生成：python3 test/gen_doc_smoke.py",
        "; 复核清单：test/data/doc_smoke.tsv（mode=skip 的条目注明原因）",
        ";=============================================================",
        "@TEST_DOC_SMOKE",
        "#DIM NONG",
        "#DIM CVTMP",
        "#DIMS CVTMP_S",
        "",
        "PRINTL == 组35：文档语义冒烟（依据 test/data 的 md） ==",
        "",
        ";---- 35.1 式中函数 ----",
    ]
    fed = []                                        # 需要 runner 注入输入的条目

    def emit(name: str, mode: str, snip: str, note: str, inp: str = "") -> None:
        if mode == "call":
            body.extend(snip.split("\\n"))          # 多行前置用 \n 分隔
            if inp.strip():
                body.append(f";   ↑ 需注入输入：{inp.strip()}")
                fed.append((name, inp.strip()))
        else:
            body.append(f"; SKIP {name} —— {note}")

    for name, _kind, mode, snip, note, inp in funcs:
        emit(name, mode, snip, note, inp)
    body += ["", ";---- 35.2 命令 ----"]
    for name, _kind, mode, snip, note, inp in cmds:
        emit(name, mode, snip, note, inp)
    if fed:
        body += ["", ";---- 35.3 需注入输入的条目（供 runner 组 script 用） ----"]
        body += [f";   {n}: {i}" for n, i in fed]
    body += [
        "",
        'CALL ASSERT_TRUE, 1, @"组35 文档语义冒烟执行完毕（无异常中断）"',
        "NONG += RESULT",
        "RETURN NONG",
        "",
    ]
    OUT_ERB.write_text("\n".join(body), encoding="utf-8")
    from collections import Counter
    c = Counter((r[1], r[2]) for r in rows)
    print(f"写出 {OUT_TSV.name} 与 {OUT_ERB.name}")
    print("函数:", {k: v for k, v in c.items() if k[0] == 'func'},
          " 命令:", {k: v for k, v in c.items() if k[0] == 'cmd'})
    return 0


if __name__ == "__main__":
    sys.exit(main())
