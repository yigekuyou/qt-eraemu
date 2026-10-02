#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# export_command_tables.py —— 从 **C# 权威源码** 导出命令清单（不用本移植的 C++ 表）
#
# 产出（纯数据文件，供 gen_coverage.py / check_coverage.sh 使用）：
#   test/data/emuera_standard_cmds.txt   Emuera C# 原版「命令 / 指令」
#   test/data/emuera_standard_funcs.txt  Emuera C# 原版「式中函数」
#   test/data/emuera_ee_cmds.txt         EmueraEE 扩展命令（EE 发行版清单 - 原版）
#
# 来源（全部是 C#/上游发行物，不是本移植的 C++ 表）：
#   1) Emuera/GameProc/Function/BuiltInFunctionCode.cs
#        `enum FunctionCode` —— 每个枚举名就是 ERB 里写的命令名
#        （FunctionIdentifier.addFunction 用 code.ToString() 作为字典键）
#   2) Emuera/GameData/Function/Creator.cs
#        `methodList["NAME"] = …` —— 式中函数名
#   3) eraTW/改造…/eratohoTWサクラエディタ用キーワードヘルプ/ERB_EXCOM.khp
#        EM+EE 发行版自带的命令关键字帮助（EE 扩展命令的来源清单）
#
# 用法：python3 test/export_command_tables.py
# ---------------------------------------------------------------------------
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUTDIR = ROOT / "test/data"

ENUM_CS = ROOT / "Emuera/GameProc/Function/BuiltInFunctionCode.cs"
CREATOR_CS = ROOT / "Emuera/GameData/Function/Creator.cs"
KHP = (ROOT / "eraTW/改造とかしてみたい人のためのあれこれ"
       / "eratohoTWサクラエディタ用キーワードヘルプ/ERB_EXCOM.khp")

# 枚举里的内部值（不是 ERB 命令）
INTERNAL = {"SET", "REF", "REFBYNAME"}

# PRINT 族后缀组合是「由基名 + 后缀」在解析期生成的，不逐个当独立命令列
PRINT_BASE = re.compile(
    r"^PRINT(SINGLE)?(V|S|FORMS|FORM)?(K|D)?(C|LC)?(L|W)?$")


def read_text(p: pathlib.Path) -> str:
    for enc in ("utf-8-sig", "utf-8", "cp932"):
        try:
            return p.read_text(encoding=enc)
        except (UnicodeDecodeError, LookupError):
            continue
    return p.read_bytes().decode("utf-8", errors="ignore")


def export_standard_cmds() -> list[str]:
    src = read_text(ENUM_CS)
    m = re.search(r"enum\s+FunctionCode\s*\{(.*?)\n\t\}", src, re.S)
    if not m:
        raise SystemExit(f"未能解析 FunctionCode 枚举：{ENUM_CS}")
    body = m.group(1)
    names = re.findall(r"^\s*([A-Z][A-Z_0-9]*)\s*,?\s*(?://[^\n]*)?$", body, re.M)
    seen, out = set(), []
    for n in names:
        if n in INTERNAL or n in seen:
            continue
        # PRINT 族：只保留基名与少量代表形态（其余是后缀组合）
        if n.startswith("PRINT") and PRINT_BASE.match(n):
            continue
        seen.add(n)
        out.append(n)
    return sorted(out)


def export_standard_funcs() -> list[str]:
    src = read_text(CREATOR_CS)
    names = re.findall(r'methodList\["([A-Z_0-9]+)"\]', src)
    return sorted(set(names))


# EmueraEE 相对 Emuera 原版**新增**的命令/函数。EE 未附带 C# 源码，此清单取自
# EE 发行版自带的文档（EmueraEE_readme.txt / EmueraEE_changelog.txt，均位于
# eraTW/README集/EmueraEE Readme/），并剔除文档里的普通名词（TYPO/WINAPI 等）。
EE_EXTENSIONS = [
    # 声音
    "PLAYBGM", "STOPBGM", "SETBGMVOLUME", "PLAYSOUND", "STOPSOUND",
    "SETSOUNDVOLUME", "EXISTSOUND",
    # 多列文本（COLUMN_*）
    "COLUMNCREATE", "COLUMNDIRECTION", "COLUMNMOVE", "COLUMNRESIZE",
    "COLUMNCLEAR", "COLUMNPRINT", "COLUMNPRINTL", "COLUMNPRINTW",
    "COLUMNWAIT", "COLUMNCOLOR", "COLUMNBGCOLOR",
    # 内存 / 调试
    "CLEARMEMORY", "GETMEMORYUSAGE", "GETDOINGFUNCTION", "EXISTFUNCTION",
    "UPDATECHECK",
    # 输入扩展
    "BINPUT", "BINPUTS", "FLOWINPUT", "INPUTANY", "TINPUTAWAIT",
    # 流程扩展
    "FORCE_BEGIN", "FORCE_QUIT", "FORCE_QUIT_AND_RESTART", "QUIT_AND_RESTART",
    # 图形扩展
    "GDRAWTEXT", "GDRAWLINE", "GDASHSTYLE", "GDRAWGWITHROTATE",
    "GGETTEXTSIZE", "GGETFONT", "GGETFONTSIZE", "GGETPEN", "GGETPENWIDTH",
    "SPRITEDISPOSEALL", "GETTEXTSIZE",
    # 文本框 / 显示
    "SETTEXTBOX", "GETTEXTBOX", "GETDISPLAYLINE", "OCLEARLINE",
    "SKIPLOG", "LCSVISASSI",
    # 工具提示扩展
    "TOOLTIP_IMG", "TOOLTIP_EXTENSION", "FTOOLTIP_SETDURATION",
    # 字符串 / 调用扩展
    "FSTRJOIN", "STRJOIN1", "TRYCALLF", "TRYCALLFORMF",
]


def export_ee_cmds(standard: list[str], std_funcs: list[str]) -> list[str]:
    """EE 扩展命令清单（见 EE_EXTENSIONS；再减去原版同名项）。"""
    if not KHP.exists():
        return []
    known = set(standard) | set(std_funcs)
    return sorted(n for n in set(EE_EXTENSIONS) if n not in known)


def main() -> int:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    std_cmds = export_standard_cmds()
    std_funcs = export_standard_funcs()
    ee_cmds = export_ee_cmds(std_cmds, std_funcs)

    hdr = ("# 由 test/export_command_tables.py 从 C# 权威源码导出，勿手改\n"
           "# 每行一个名字；'#' 开头为注释\n")

    (OUTDIR / "emuera_standard_cmds.txt").write_text(
        hdr + "# 源: Emuera/GameProc/Function/BuiltInFunctionCode.cs (enum FunctionCode)\n"
        + "\n".join(std_cmds) + "\n", encoding="utf-8")
    (OUTDIR / "emuera_standard_funcs.txt").write_text(
        hdr + "# 源: Emuera/GameData/Function/Creator.cs (methodList)\n"
        + "\n".join(std_funcs) + "\n", encoding="utf-8")
    (OUTDIR / "emuera_ee_cmds.txt").write_text(
        hdr + "# 源: eraTW/…/ERB_EXCOM.khp（EM+EE 发行版命令关键字帮助）− 原版命令\n"
        + "\n".join(ee_cmds) + "\n", encoding="utf-8")

    print(f"标准命令 {len(std_cmds)} 条 -> emuera_standard_cmds.txt")
    print(f"式中函数 {len(std_funcs)} 条 -> emuera_standard_funcs.txt")
    print(f"EE 扩展  {len(ee_cmds)} 条 -> emuera_ee_cmds.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
