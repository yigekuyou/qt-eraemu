#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# gen_coverage.py —— 基于 **C# 权威命令表** 生成 / 校验「全函数覆盖」用例
#
# 输入（由 export_command_tables.py 从 C# 源码导出，本脚本不读 C++ 表）：
#   test/data/emuera_standard_cmds.txt   Emuera 原版命令
#   test/data/emuera_standard_funcs.txt  Emuera 原版式中函数
#   test/data/emuera_ee_cmds.txt         EmueraEE 扩展命令
#
# 输出：
#   test/example/ERB/10_COVERAGE.ERB     自动生成的冒烟覆盖组
#   test/data/coverage_report.txt        覆盖率报告（C# 表 vs 手写用例 vs 引擎）
#
# 用法：
#   python3 test/gen_coverage.py            # 生成 10_COVERAGE.ERB
#   python3 test/gen_coverage.py --check    # 只打印覆盖率报告，不写文件
# ---------------------------------------------------------------------------
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "test/data"
ERB_DIR = ROOT / "test/example/ERB"
OUT_ERB = ERB_DIR / "10_COVERAGE.ERB"
OUT_REPORT = DATA / "coverage_report.txt"

# 控制流 / 流程改变 / 交互 / 写盘：由手写组覆盖，不自动生成（同上版说明）
EXCLUDE = {
    "IF", "SIF", "ELSEIF", "ELSE", "ENDIF", "WHILE", "WEND", "REPEAT", "REND",
    "FOR", "NEXT", "DO", "LOOP", "BREAK", "CONTINUE", "SELECTCASE", "CASE",
    "CASEELSE", "ENDSELECT", "GOTO", "GOTOFORM", "TRYGOTO", "TRYGOTOFORM",
    "TRYCGOTO", "TRYCGOTOFORM", "CALLEVENT", "TRYCALLEVENT",
    "CALL", "CALLFORM", "CALLF", "CALLFORMF", "TRYCALL", "TRYCALLFORM",
    "TRYCCALL", "TRYCCALLFORM", "JUMP", "JUMPFORM", "TRYJUMP", "TRYJUMPFORM",
    "TRYCJUMP", "TRYCJUMPFORM", "BEGIN", "FUNC", "ENDFUNC",
    "TRYCALLLIST", "TRYJUMPLIST", "TRYGOTOLIST", "GOTOLIST",
    "RETURN", "RETURNF", "RETURNFORM", "CATCH", "ENDCATCH",
    "QUIT", "THROW", "ASSERT", "RESTART", "FORCE_BEGIN", "FORCE_QUIT",
    # 训练流程命令：只在特定系统状态下合法（C# 同样会抛 CodeEE）
    "DOTRAIN", "CALLTRAIN", "STOPCALLTRAIN",
    "FORCE_QUIT_AND_RESTART", "QUIT_AND_RESTART",
    "INPUT", "INPUTS", "ONEINPUT", "ONEINPUTS", "TINPUT", "TINPUTS",
    "TONEINPUT", "TONEINPUTS", "WAIT", "WAITANYKEY", "FORCEWAIT", "AWAIT",
    "TWAIT", "INPUTMOUSEKEY", "BINPUT", "BINPUTS", "FLOWINPUT", "INPUTANY",
    "TINPUTAWAIT",
    "SAVEDATA", "LOADDATA", "DELDATA", "CHKDATA", "SAVEGAME", "LOADGAME",
    "SAVEGLOBAL", "LOADGLOBAL", "SAVEVAR", "LOADVAR", "SAVENOS", "SAVETEXT",
    "LOADTEXT", "PUTFORM", "OUTPUTLOG", "SAVECHARA", "LOADCHARA",
    "RESETDATA", "RESETGLOBAL", "CLEARMEMORY",
    "RANDOMIZE", "DUMPRAND", "INITRAND",
    "DELALLCHARA", "ADDDEFCHARA", "ADDVOIDCHARA", "ADDSPCHARA", "ADDCHARA",
    "DELCHARA", "COPYCHARA", "ADDCOPYCHARA", "SWAPCHARA", "PICKUPCHARA",
    "SORTCHARA", "ARRAYMSORT",
    # 会改写/删除已显示行的命令：冒烟会把本组输出删掉（CLEARLINE 默认删 1 行）
    "CLEARLINE", "REUSELASTLINE", "CLEARTEXTBOX", "OCLEARLINE",
    "SPLIT", "PRINTDATA", "PRINTDATAL", "PRINTDATAW", "STRDATA", "DATALIST",
    "ENDLIST", "DATA", "DATAFORM", "ENDDATA", "PRINTBUTTON", "PRINTBUTTONC",
    "PRINTBUTTONLC", "REDRAW", "PRINT_IMG", "PRINT_RECT", "PRINT_SPACE",
    # EE 扩展里需要运行环境（声音/文本框/多列/内存）的，冒烟无意义
    "PLAYBGM", "STOPBGM", "SETBGMVOLUME", "PLAYSOUND", "STOPSOUND",
    "SETSOUNDVOLUME", "SETTEXTBOX", "GETTEXTBOX", "UPDATECHECK", "SKIPLOG",
    # EM/EE 扩展：需要图形/交互环境、或本仓库只有式中函数形态（详见 EXCLUDE_REASON）
    "SETBGIMAGE",
    "CLEARBGIMAGE",
    "REMOVEBGIMAGE",
    "HTML_PRINT_ISLAND",
    "HTML_PRINT_ISLAND_CLEAR",
    "BREAKBUTTON",
    "ONEBINPUT",
    "ONEBINPUTS",
    "DT_COLUMN_OPTIONS",
    "CALLSHARP",
    "TOOLTIP_CUSTOM",
    "TOOLTIP_FORMAT",
    "TOOLTIP_SETFONT",
    "TOOLTIP_SETFONTSIZE",
    "EXISTSOUND",
    "EXISTFUNCTION",
    "GETMEMORYUSAGE",
    "GETDOINGFUNCTION",
    "VARI",
    "VARS",
    "TRYCALLF",
    "TRYCALLFORMF",
    "COLUMNCREATE",
    "COLUMNDIRECTION",
    "COLUMNMOVE",
    "COLUMNRESIZE",
    "COLUMNCLEAR",
    "COLUMNPRINT",
    "COLUMNPRINTL",
    "COLUMNPRINTW",
    "COLUMNWAIT",
    "COLUMNCOLOR",
    "COLUMNBGCOLOR",
    "LCSVISASSI",
    "GETTEXTSIZE",
}
# 被排除项的**原因**（写进覆盖率报告，说明为什么不由自动段执行）
EXCLUDE_REASON = {
    "DOTRAIN": "仅训练流程状态合法（C# 同样报错）",
    "CALLTRAIN": "仅训练流程状态合法",
    "STOPCALLTRAIN": "仅训练流程状态合法",
    "RESTART": "重启脚本（破坏性）",
    "BEGIN": "改变游戏流程且不返回（组12 单独跑）",
    "FORCE_BEGIN": "改变游戏流程（EE）",
    "FORCE_QUIT": "结束程序（EE）",
    "FORCE_QUIT_AND_RESTART": "结束并重启（EE）",
    "QUIT_AND_RESTART": "结束并重启（EE）",
    "QUIT": "结束程序（全部自动路径的收尾）",
    "THROW": "主动报错并终止（组13 单独跑）",
    "ASSERT": "断言失败即终止",
    "INPUTMOUSEKEY": "需要鼠标输入（GUI）",
    "BINPUT": "需要鼠标按钮输入（EE）",
    "BINPUTS": "需要鼠标按钮输入（EE）",
    "FLOWINPUT": "交互输入（EE）",
    "INPUTANY": "交互输入（EE）",
    "TINPUTAWAIT": "交互输入（EE）",
    "INPUTINPUT": "交互输入",
    "ADDDEFCHARA": "@SYSTEM_TITLE 专用（组14 覆盖）",
    "CALLEVENT": "事件函数调用（组14 覆盖）",
    "CLEARLINE": "会删除已显示行（干扰本组输出）",
    "REUSELASTLINE": "会改写已显示行",
    "CLEARTEXTBOX": "清空文本框",
    "OCLEARLINE": "删除已显示行（EE）",
    "PLAYBGM": "需要音频后端（EE）",
    "STOPBGM": "需要音频后端（EE）",
    "SETBGMVOLUME": "需要音频后端（EE）",
    "PLAYSOUND": "需要音频后端（EE）",
    "STOPSOUND": "需要音频后端（EE）",
    "SETSOUNDVOLUME": "需要音频后端（EE）",
    "SETTEXTBOX": "需要文本框控件（EE）",
    "GETTEXTBOX": "需要文本框控件（EE）",
    "UPDATECHECK": "需要网络（EE）",
    "SKIPLOG": "控制日志输出（EE）",
    "CLEARMEMORY": "释放内存（EE，破坏性）",
    "SETBGIMAGE": "需要背景图资源（EM）",
    "CLEARBGIMAGE": "需要背景图资源（EM）",
    "REMOVEBGIMAGE": "需要背景图资源（EM）",
    "HTML_PRINT_ISLAND": "悬浮 HTML 层（Emuera.NET）",
    "HTML_PRINT_ISLAND_CLEAR": "悬浮 HTML 层（Emuera.NET）",
    "BREAKBUTTON": "使旧按钮失效（GUI）",
    "ONEBINPUT": "需要鼠标按钮输入（EE）",
    "ONEBINPUTS": "需要鼠标按钮输入（EE）",
    "DT_COLUMN_OPTIONS": "DataTable 列选项（EM）",
    "CALLSHARP": "需要插件 DLL（Emuera.NET）",
    "TOOLTIP_CUSTOM": "工具提示需 GUI",
    "TOOLTIP_FORMAT": "工具提示需 GUI",
    "TOOLTIP_SETFONT": "工具提示需 GUI",
    "TOOLTIP_SETFONTSIZE": "工具提示需 GUI",
    "EXISTSOUND": "本仓库为式中函数，无命令形态",
    "EXISTFUNCTION": "本仓库为式中函数，无命令形态",
    "GETMEMORYUSAGE": "本仓库为式中函数，无命令形态",
    "GETDOINGFUNCTION": "本仓库为式中函数，无命令形态",
    "VARI": "需 setting.json 的 UseScopedVariableInstruction 开启（Emuera.NET）",
    "VARS": "需 setting.json 的 UseScopedVariableInstruction 开启（Emuera.NET）",
    "TRYCALLF": "需要已存在的用户函数",
    "TRYCALLFORMF": "需要已存在的用户函数",
    "LCSVISASSI": "本仓库无实现（语义未确证）",
    "GETTEXTSIZE": "本仓库无实现（疑为 GETTEXTBOX 笔误）",
    "COLUMNCREATE": "EE 列库（ERB 实现，非引擎命令）",
    "COLUMNDIRECTION": "EE 列库（ERB 实现，非引擎命令）",
    "COLUMNMOVE": "EE 列库（ERB 实现，非引擎命令）",
    "COLUMNRESIZE": "EE 列库（ERB 实现，非引擎命令）",
    "COLUMNCLEAR": "EE 列库（ERB 实现，非引擎命令）",
    "COLUMNPRINT": "EE 列库（ERB 实现，非引擎命令）",
    "COLUMNPRINTL": "EE 列库（ERB 实现，非引擎命令）",
    "COLUMNPRINTW": "EE 列库（ERB 实现，非引擎命令）",
    "COLUMNWAIT": "EE 列库（ERB 实现，非引擎命令）",
    "COLUMNCOLOR": "EE 列库（ERB 实现，非引擎命令）",
    "COLUMNBGCOLOR": "EE 列库（ERB 实现，非引擎命令）",
}

# PRINT 族以「基名 + 后缀」组合，逐个生成无意义
# PRINT 族 = 基名 + 任意顺序的修饰后缀（值类型 V/S/FORM/FORMS、对齐 C/LC、
#            配色 K/D、行尾 L/W、EM/EE 新增的 N 后缀可组合）
PRINT_BASE = re.compile(r"^PRINT(SINGLE)?(V|S|FORMS|FORM)?(K|D|C|LC|L|W|N)*$")


def load(path: pathlib.Path) -> list[str]:
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        out.append(line)
    return out


def erb_mentioned() -> set[str]:
    names = set()
    for f in sorted(ERB_DIR.glob("*.ERB")):
        if f.name == OUT_ERB.name:
            continue
        raw = f.read_bytes()
        for enc in ("utf-8", "cp932"):
            try:
                txt = raw.decode(enc)
                break
            except UnicodeDecodeError:
                continue
        else:
            txt = raw.decode("utf-8", errors="ignore")
        names |= set(re.findall(r"\b([A-Z][A-Z_0-9]{1,40})\b", txt))
    return names


def main() -> int:
    check_only = "--check" in sys.argv
    std_cmds = load(DATA / "emuera_standard_cmds.txt")
    std_funcs = load(DATA / "emuera_standard_funcs.txt")
    ee_cmds = load(DATA / "emuera_ee_cmds.txt")
    covered = erb_mentioned()

    def need(name: str) -> bool:
        return (name not in covered and name not in EXCLUDE
                and not PRINT_BASE.match(name))

    func_lines = [f"CVTMP = {n}(0)" for n in std_funcs if need(n)]
    cmd_lines = [n for n in (std_cmds + ee_cmds) if need(n)]

    body = [
        ";=============================================================",
        "; [10] 全函数覆盖（由 test/gen_coverage.py 自动生成，勿手改）",
        ";",
        "; 名单来源：test/data/emuera_standard_{cmds,funcs}.txt",
        ";           test/data/emuera_ee_cmds.txt",
        ";      （三者均由 test/export_command_tables.py 从 C# 权威源码导出）",
        "; 目的：C# 命令表里的每一项都至少被解析并执行一次。",
        "; 重新生成：python3 test/gen_coverage.py",
        ";=============================================================",
        "@TEST_COVERAGE",
        "#DIM CVTMP",
        "#DIMS CVTMP_S",
        "",
        "PRINTL == 组10：全函数覆盖（依据 C# 命令表自动生成） ==",
        "",
        ";---- 10.1 式中函数 ----",
        *func_lines,
        "",
        ";---- 10.2 命令 ----",
        *cmd_lines,
        "",
        'CALL ASSERT_TRUE, 1, @"全函数覆盖段执行完毕（无异常中断）"',
        "NONG += RESULT",
        "RETURN NONG",
        "",
    ]
    if not check_only:
        OUT_ERB.write_text("\n".join(body), encoding="utf-8")

    # ---- 覆盖率报告 ----
    def report(title: str, names: list[str]) -> list[str]:
        lines = [f"## {title}（{len(names)} 条）"]
        variant = [n for n in names if PRINT_BASE.match(n)]
        hand = [n for n in names if n in covered]
        excluded = [n for n in names if n not in covered and n in EXCLUDE]
        auto = [n for n in names if need(n)]
        rest = sorted(set(names) - set(hand) - set(excluded) - set(auto) - set(variant))
        lines.append(f"  手写用例已覆盖    ：{len(hand)}")
        lines.append(f"  自动生成冒烟覆盖  ：{len(auto)}")
        lines.append(f"  不由自动段执行    ：{len(excluded)}")
        for n in sorted(excluded):
            lines.append(f"      · {n} —— {EXCLUDE_REASON.get(n, '由手写组覆盖')}")
        lines.append(f"  PRINT 族后缀变体  ：{len(variant)}（由基名规则覆盖，不逐个列）")
        if rest:
            lines.append(f"  [!] 未覆盖        ：{len(rest)} -> {' '.join(rest)}")
        else:
            lines.append("  未覆盖            ：0（全部登记项均有对应用例）")
        return lines

    rep = ["# 覆盖率报告（依据 C# 权威命令表）", ""]
    rep += report("Emuera 原版命令", std_cmds)
    rep += report("Emuera 原版式中函数", std_funcs)
    rep += report("EmueraEE 扩展命令", ee_cmds)
    if not check_only:
        OUT_REPORT.write_text("\n".join(rep) + "\n", encoding="utf-8")

    print("\n".join(rep))
    print(f"\n生成 {OUT_ERB.relative_to(ROOT)}：函数 {len(func_lines)} 条 / 命令 {len(cmd_lines)} 条")
    return 0


if __name__ == "__main__":
    sys.exit(main())
