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
#   1) 各棵 C# 源码树的 BuiltInFunctionCode.cs
#        `enum FunctionCode` —— 每个枚举名就是 ERB 里写的命令名
#        （FunctionIdentifier.addFunction 用 code.ToString() 作为字典键）
#        本仓库存在多棵树（重构版 emuera.em/Emuera 是超集，另有经典布局的
#        Emuera/ 与 eraTW 内置的补丁树），脚本取**全部树的并集**，避免漏收。
#   2) 各棵树的 Creator.cs
#        `methodList["NAME"] = …` —— 式中函数名
#   3) eraTW/改造…/eratohoTWサクラエディタ用キーワードヘルプ/ERB_EXCOM.khp
#        EM+EE 发行版自带的命令关键字帮助（EE 扩展命令的来源清单）
#
# 分类规则：
#   emuera_standard_cmds.txt  = 原版（经典布局树）枚举 − INTERNAL
#   emuera_ee_cmds.txt        = (超集树枚举 − 原版枚举) ∪ EE 文档独有名字
# 两个清单的并集 == 全部树的枚举成员，既不漏也不重。
#
# 用法：python3 test/export_command_tables.py
# ---------------------------------------------------------------------------
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUTDIR = ROOT / "test/data"

# 候选源码树（枚举/函数表实际所在路径，按优先级列出；取并集）
TREES = [
    ROOT / "emuera.em/Emuera",
    ROOT / "Emuera",
    ROOT / "eraTW/パッチ/Emuera1824+v11+webp+test+fix/src1824+v11+webp+test+fix/Emuera",
]
# 原版基线树（用于判定「哪些枚举成员属于 EM/EE 扩展」）
BASELINE_TREES = [
    ROOT / "Emuera",
    ROOT / "eraTW/パッチ/Emuera1824+v11+webp+test+fix/src1824+v11+webp+test+fix/Emuera",
]
KHP = (ROOT / "eraTW/改造とかしてみたい人のためのあれこれ"
       / "eratohoTWサクラエディタ用キーワードヘルプ/ERB_EXCOM.khp")

# 枚举里的内部值（不是 ERB 命令）
INTERNAL = {"SET", "REF", "REFBYNAME"}

ENUM_REL = [
    "Runtime/Script/Statements/BuiltInFunctionCode.cs",  # 重构版布局
    "GameProc/Function/BuiltInFunctionCode.cs",          # 经典布局
]
CREATOR_REL = [
    "Runtime/Script/Statements/Function/Creator.cs",
    "GameData/Function/Creator.cs",
]


def find(tree: pathlib.Path, rels: list[str]) -> pathlib.Path | None:
    for rel in rels:
        p = tree / rel
        if p.exists():
            return p
    return None


def read_text(p: pathlib.Path) -> str:
    for enc in ("utf-8-sig", "utf-8", "cp932"):
        try:
            return p.read_text(encoding=enc)
        except (UnicodeDecodeError, LookupError):
            continue
    return p.read_bytes().decode("utf-8", errors="ignore")


def enum_members(tree: pathlib.Path) -> set[str]:
    """读一棵树的 FunctionCode 枚举成员（去掉内部值）。"""
    p = find(tree, ENUM_REL)
    if p is None:
        return set()
    src = read_text(p)
    # 两种布局的收尾缩进不同（经典版 `\n\t}`、重构版顶级 `\n}`），取行首的 `}`
    m = re.search(r"enum\s+FunctionCode\s*\{(.*?)\n\s*\}", src, re.S)
    if not m:
        raise SystemExit(f"未能解析 FunctionCode 枚举：{p}")
    names = re.findall(r"^\s*([A-Z][A-Z_0-9]*)\s*,?\s*(?://[^\n]*)?$", m.group(1), re.M)
    return set(names)


def creator_funcs(tree: pathlib.Path) -> set[str]:
    p = find(tree, CREATOR_REL)
    if p is None:
        return set()
    return set(re.findall(r'methodList\["([A-Z_0-9]+)"\]', read_text(p)))


def export_standard_cmds() -> list[str]:
    """原版命令 = 经典布局基线树的枚举 − 内部值。保留全部枚举成员（含 PRINT
    族后缀变体）——清单要忠实反映枚举，是否逐个冒烟由 gen_coverage.py 的
    PRINT_BASE 规则决定。"""
    names: set[str] = set()
    for t in BASELINE_TREES:
        names |= enum_members(t)
    if not names:
        raise SystemExit("未找到任何基线树的 FunctionCode 枚举")
    return sorted(names - INTERNAL)


def export_standard_funcs() -> list[str]:
    names: set[str] = set()
    for t in TREES:
        names |= creator_funcs(t)
    return sorted(names)


# EE 文档独有、但**不在任何 C# 枚举里**的名字（EE 发行版以 ERB 库/式中函数形式
# 提供，或仅见于 EE readme）。这些只能从文档清单来，故在此显式维护。
EE_DOC_ONLY = [
    # 多列文本列库（ERB 实现，见 eraTW/README集/EmueraEE Readme/read me(COLUMN_LIB).txt）
    "COLUMNCREATE", "COLUMNDIRECTION", "COLUMNMOVE", "COLUMNRESIZE",
    "COLUMNCLEAR", "COLUMNPRINT", "COLUMNPRINTL", "COLUMNPRINTW",
    "COLUMNWAIT", "COLUMNCOLOR", "COLUMNBGCOLOR",
    # 只在 EE readme 出现、本仓库 C# 侧无对应枚举的条目
    # （GETTEXTBOX 的笔误 GETTEXTSIZE、以及式中函数形态的 EXISTSOUND/
    #   CLEARMEMORY/GETMEMORYUSAGE/GETDOINGFUNCTION/EXISTFUNCTION/FLOWINPUT 等）
    "GETTEXTSIZE", "LCSVISASSI", "OCLEARLINE", "EXISTSOUND",
    "CLEARMEMORY", "GETMEMORYUSAGE", "GETDOINGFUNCTION", "EXISTFUNCTION",
    "FLOWINPUT",
]
# 注意：EE readme 里出现的 TOOLTIP_EXTENSION 是**文档站页面名**而非命令
# （页面内记载的是 TOOLTIP_CUSTOM/SETFONT/SETFONTSIZE/FORMAT），故不列入清单；
# 早期版本由 Shift-JIS 文本切分误产生的伪名（FSTRJOIN / FTOOLTIP_SETDURATION /
# STRJOIN1 / TINPUTAWAIT）也已移除，其真实指向分别是 STRJOIN、TOOLTIP_SETDURATION
# 与「TINPUT 与 AWAIT 的挙動変更」一句。


def export_ee_cmds(standard: list[str]) -> list[str]:
    """EE 扩展命令 = (超集树枚举 − 原版枚举) ∪ EE 文档独有名字。

    超集（重构版）树里有、基线树里没有的枚举成员，即 EM/EE 扩展；
    文档独有名再补上不在枚举里的部分。两者都不与原版清单重叠。
    """
    baseline: set[str] = set()
    for t in BASELINE_TREES:
        baseline |= enum_members(t)
    superset: set[str] = set()
    for t in TREES:
        superset |= enum_members(t)
    names = ((superset - baseline) - INTERNAL) | set(EE_DOC_ONLY)
    return sorted(names - set(standard))


def main() -> int:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    std_cmds = export_standard_cmds()
    std_funcs = export_standard_funcs()
    ee_cmds = export_ee_cmds(std_cmds)

    hdr = ("# 由 test/export_command_tables.py 从 C# 权威源码导出，勿手改\n"
           "# 每行一个名字；'#' 开头为注释\n")

    (OUTDIR / "emuera_standard_cmds.txt").write_text(
        hdr + "# 源: 经典布局基线树 Emuera/GameProc/Function/BuiltInFunctionCode.cs\n"
        "#     (enum FunctionCode；含 PRINT 族全部后缀变体，忠实反映枚举)\n"
        + "\n".join(std_cmds) + "\n", encoding="utf-8")
    (OUTDIR / "emuera_standard_funcs.txt").write_text(
        hdr + "# 源: 各棵树的 Creator.cs (methodList) 并集\n"
        + "\n".join(std_funcs) + "\n", encoding="utf-8")
    (OUTDIR / "emuera_ee_cmds.txt").write_text(
        hdr + "# 源: (超集树 emuera.em/Emuera 枚举 − 原版枚举) ∪ EE 文档独有名字\n"
        + "\n".join(ee_cmds) + "\n", encoding="utf-8")

    # 自检：两清单并集必须覆盖全部树的枚举成员，且两清单不相交
    allenum: set[str] = set()
    for t in TREES:
        allenum |= enum_members(t)
    std_s, ee_s = set(std_cmds), set(ee_cmds)
    missed = sorted((allenum - INTERNAL) - std_s - ee_s)
    both = sorted(std_s & ee_s)
    if missed:
        print(f"[!] 仍未收录的枚举成员 {len(missed)}: {missed}")
    if both:
        print(f"[!] 两清单重复 {len(both)}: {both}")
    if not missed and not both:
        print(f"[OK] 枚举成员 {len(allenum - INTERNAL)} 条全部收录，两清单无重复")

    print(f"标准命令 {len(std_cmds)} 条 -> emuera_standard_cmds.txt")
    print(f"式中函数 {len(std_funcs)} 条 -> emuera_standard_funcs.txt")
    print(f"EE 扩展  {len(ee_cmds)} 条 -> emuera_ee_cmds.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
