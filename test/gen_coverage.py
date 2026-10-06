#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# gen_coverage.py —— 依据 **C# 权威命令表** 出「覆盖率报告」（不再生成 ERB）
#
# 用例生成已统一到 gen_doc_smoke.py：
#   test/data/doc_smoke.tsv  →  test/example/ERB/35_DOC_SMOKE.ERB（组35）
# 本脚本只**测量**，不写任何 ERB（旧的全函数覆盖组 10_COVERAGE.ERB 已移除）。
#
# 输入：
#   test/data/emuera_standard_cmds.txt   Emuera 原版命令（export_command_tables.py 导出）
#   test/data/emuera_standard_funcs.txt  Emuera 原版式中函数（同上）
#   test/data/emuera_ee_cmds.txt         EmueraEE 扩展命令（同上）
#   test/data/doc_smoke.tsv              复核后的文档冒烟清单（组35 的唯一输入）
#   test/example/ERB/*.ERB               实际用例（组1..33 手写 + 组35）
#
# 输出：
#   test/data/coverage_report.txt        覆盖率报告
#
# 判定规则（本脚本不自己编「不测试」的理由）：
#   · 已有用例执行：名字出现在 example/ERB 的**代码行**里
#     （`; SKIP …` / `//` 注释行不算——否则被跳过的条目会伪装成已测试）；
#   · 跳过（未执行）：doc_smoke.tsv 里 mode=skip，note 即原因（唯一事实来源）；
#   · 未覆盖       ：以上都不是——真正的缺口。
#
# 唯一合法的「跳过」理由 = **会破坏周围状态**（按钮世代 / 显示缓冲 / 角色列表 /
# 全局变量 / 存档文件 / 流程），这类命令不能与其它用例同组跑，改在**隔离的单跑组**
# 里覆盖（如组12 破坏流程、组16 显示行、组09 存档）；实现形式（原生 / ERB 库 /
# DLL 插件 / Emuera.NET）不构成跳过理由。
#   「引擎桩」（ee_extension.cpp 的 kEeCommands：只登记名字、留痕跳过、真实现
#   待补全）单列计数，用作**完成度**参考。
#
# 用法：
#   python3 test/gen_coverage.py            # 写 coverage_report.txt
#   python3 test/gen_coverage.py --check    # 只打印，不写文件
# ---------------------------------------------------------------------------
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "test/data"
ERB_DIR = ROOT / "test/example/ERB"
OUT_REPORT = DATA / "coverage_report.txt"
SMOKE_TSV = DATA / "doc_smoke.tsv"

# 结构性/流程性条目不在文档清单里，原因写死在此（其余一律取自 doc_smoke.tsv）。
EXCLUDE_REASON_EXTRA: dict[str, str] = {}

# EE 扩展命令里**引擎只登记名字**（留痕跳过、真实现待补全）的名单——
# 来源 src/eraengine/GameProc/ee_extension.cpp 的 kEeCommands。
# 报告用它把「已执行」再分成「真实现 / 桩（待补全）」，给出完成度参考。
EE_STUB = {
    "BINPUT", "BINPUTS", "CLEARMEMORY",
    "COLUMNBGCOLOR", "COLUMNCLEAR", "COLUMNCOLOR", "COLUMNCREATE",
    "COLUMNDIRECTION", "COLUMNMOVE", "COLUMNPRINT", "COLUMNPRINTL",
    "COLUMNPRINTW", "COLUMNRESIZE", "COLUMNWAIT",
    "FLOWINPUT", "FORCE_BEGIN", "FORCE_QUIT", "FORCE_QUIT_AND_RESTART",
    "GETMEMORYUSAGE", "GETTEXTBOX", "GETTEXTSIZE", "INPUTANY", "LCSVISASSI",
    "OCLEARLINE", "QUIT_AND_RESTART", "SETTEXTBOX", "SKIPLOG", "TINPUTAWAIT",
    "TRYCALLF", "TRYCALLFORMF", "UPDATECHECK",
}

# PRINT 族以「基名 + 后缀」组合，逐个列无意义
# PRINT 族 = 基名 + 任意顺序的修饰后缀（值类型 V/S/FORM/FORMS、对齐 C/LC、
#            配色 K/D、行尾 L/W、EM/EE 新增的 N 后缀可组合）
PRINT_BASE = re.compile(r"^PRINT(SINGLE)?(V|S|FORMS|FORM)?(K|D|C|LC|L|W|N)*$")


def _short(note: str) -> str:
    """把复核 note 压成报告里的一行原因（取首分句/首句，限长）。"""
    for sep in ("——", "；", "。"):
        if sep in note:
            note = note.split(sep)[0]
            break
    note = note.strip()
    return note if len(note) <= 64 else note[:62] + "…"


def smoke_skip_reasons() -> dict[str, str]:
    """doc_smoke.tsv 里 mode=skip 条目的原因（单一事实来源，不另抄一份）。"""
    reasons: dict[str, str] = {}
    if not SMOKE_TSV.exists():
        return reasons
    for line in SMOKE_TSV.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) < 3 or cols[2] != "skip":
            continue
        note = cols[4].strip() if len(cols) > 4 else ""
        reasons[cols[0]] = _short(note) if note else "复核清单判为不宜自动执行"
    return reasons


def load(path: pathlib.Path) -> list[str]:
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        out.append(line)
    return out


def erb_mentioned() -> set[str]:
    """example 各 ERB 里**代码行**出现的名字（注释行与字符串字面量不算）。

    关键：
      · 不把 `; SKIP XXX —— …` 这种说明行算成「已覆盖」——否则跳过的条目会伪装成
        已测试；
      · 也不把字符串字面量（尤其 `CALL ASSERT_*, …, @"…XXX…"` 的断言消息）算成
        「已覆盖」——在断言文案里提到某命令名 ≠ 调用过它。
    """
    names = set()
    for f in sorted(ERB_DIR.glob("*.ERB")):
        raw = f.read_bytes()
        for enc in ("utf-8", "cp932"):
            try:
                txt = raw.decode(enc)
                break
            except UnicodeDecodeError:
                continue
        else:
            txt = raw.decode("utf-8", errors="ignore")
        for line in txt.splitlines():
            s = line.strip()
            if not s or s.startswith(";") or s.startswith("//"):
                continue
            s = re.sub(r'@?"[^"]*"', " ", s)
            s = re.sub(r"'[^']*'", " ", s)
            names |= set(re.findall(r"\b([A-Z][A-Z_0-9]{1,40})\b", s))
    return names


def main() -> int:
    check_only = "--check" in sys.argv
    std_cmds = load(DATA / "emuera_standard_cmds.txt")
    std_funcs = load(DATA / "emuera_standard_funcs.txt")
    ee_cmds = load(DATA / "emuera_ee_cmds.txt")
    covered = erb_mentioned()
    reasons = {**smoke_skip_reasons(), **EXCLUDE_REASON_EXTRA}

    def report(title: str, names: list[str]) -> list[str]:
        lines = [f"## {title}（{len(names)} 条）"]
        variant = [n for n in names if PRINT_BASE.match(n)]
        hand = [n for n in names if n in covered]
        skipped = [n for n in names if n not in covered and n in reasons]
        rest = sorted(set(names) - set(hand) - set(skipped) - set(variant))
        stubs = sorted(set(hand) & EE_STUB)
        lines.append(f"  已有用例执行      ：{len(hand)}")
        lines.append(f"  跳过（未执行）    ：{len(skipped)}")
        if stubs:
            lines.append(f"    其中引擎桩（仅登记名字·留痕跳过，真实现待补全）：{len(stubs)}")
        for n in sorted(skipped):
            lines.append(f"      · {n} —— {reasons[n]}")
        lines.append(f"  PRINT 族后缀变体  ：{len(variant)}（由基名规则覆盖，不逐个列）")
        if rest:
            lines.append(f"  [!] 未覆盖        ：{len(rest)} -> {' '.join(rest)}")
        else:
            lines.append("  未覆盖            ：0（全部登记项均有用例或已登记跳过原因）")
        return lines

    rep = [
        "# 覆盖率报告（依据 C# 权威命令表）",
        "#",
        "# 用例来源：test/example/ERB —— 手写组 + 组35（由 gen_doc_smoke.py 生成）。",
        "# 判定：代码行出现=已执行；doc_smoke.tsv mode=skip=跳过（附原因）；其余=未覆盖。",
        "",
    ]
    rep += report("Emuera 原版命令", std_cmds)
    rep += report("Emuera 原版式中函数", std_funcs)
    rep += report("EmueraEE 扩展命令", ee_cmds)
    if not check_only:
        OUT_REPORT.write_text("\n".join(rep) + "\n", encoding="utf-8")

    print("\n".join(rep))
    return 0


if __name__ == "__main__":
    sys.exit(main())
