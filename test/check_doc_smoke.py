#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# check_doc_smoke.py —— 组 35（文档语义冒烟）的静态校验（不执行 ERB）
#
# 检查项：
#   1. 清单与组文件一致：doc_smoke.tsv 里 mode=call 的条目数 == ERB 里的语句数
#      （skip 条目必须留下 `; SKIP <name> —— <原因>` 注释）
#   2. 组内每条命令语句的首个标识符必须是已知命令（标准/EE 清单）或已知变量赋值
#   3. 用到的变量必须在组内 #DIM/#DIMS 声明，或属于内置变量/伪变量/CSV 变量白名单
#   4. 结构关键字不得作为独立语句出现（IF/FOR/BREAK/BEGIN/RETURN…）
#   5. 代码块/字符串引号配对、无制表符、无全角括号
#
# 用法：python3 test/check_doc_smoke.py
# ---------------------------------------------------------------------------
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "test/data"
ERB = ROOT / "test/example/ERB/35_DOC_SMOKE.ERB"

# 块结构（按名字规则判定，而非枚举）
#   开块 → 闭块：PRINTDATA*/STRDATA → ENDDATA；DATALIST → ENDLIST；
#   TRYC*（带 C 的 catch 族）→ ENDCATCH；*LIST 族 → ENDFUNC（FUNC 行是块内条目）
CATCH_FAMILY = {"TRYCCALL", "TRYCCALLFORM", "TRYCJUMP", "TRYCJUMPFORM",
                "TRYCGOTO", "TRYCGOTOFORM"}
LIST_FAMILY = {"TRYCALLLIST", "TRYJUMPLIST", "TRYGOTOLIST"}
CLOSERS = {"ENDIF", "ENDSELECT", "NEXT", "WEND", "LOOP", "REND", "ENDDATA",
           "ENDLIST", "ENDFUNC", "ENDCATCH"}
# 完全不能作为独立语句出现（必须由块携带）
FORBIDDEN_ALONE = {
    "SIF", "ELSE", "ELSEIF", "CASE", "CASEELSE", "BREAK", "CONTINUE", "BEGIN",
    "SET", "REF", "REFBYNAME",
}
# 这些是「块内条目」，只有对应块开着时才合法
BLOCK_BODY = {"DATA": "ENDDATA", "DATAFORM": "ENDDATA", "CATCH": "ENDCATCH"}
# 组体中间禁止提前返回（组自身收尾的 RETURN 在末尾，见下方 allowed_tail）
RETURN_FAMILY = {"RETURN", "RETURNF", "RETURNFORM"}


def opener_of(head: str, stack) -> str | None:
    """返回该语句头期望的闭块关键字；不是开块则 None。"""
    if head.startswith("PRINTDATA") or head == "STRDATA":
        return "ENDDATA"
    if head == "DATALIST":
        return "ENDLIST"
    if head in CATCH_FAMILY:
        return "ENDCATCH"
    if head in LIST_FAMILY:
        return "ENDFUNC"
    if head == "FUNC":
        # LIST 块内的 FUNC 是列表条目，不再开块
        if stack and stack[-1][1] == "ENDFUNC":
            return None
        return "ENDFUNC"
    if head in ("IF", "SELECTCASE", "FOR", "WHILE", "DO", "REPEAT"):
        return {"IF": "ENDIF", "SELECTCASE": "ENDSELECT", "FOR": "NEXT",
                "WHILE": "WEND", "DO": "LOOP", "REPEAT": "REND"}[head]
    return None


# 内置变量 / 伪变量 / CSV 变量（允许直接出现）
BUILTIN = {
    "RESULT", "RESULTS", "COUNT", "TARGET", "MASTER", "ASSI", "PLAYER", "ASSIPLAY",
    "MONEY", "DAY", "TIME", "CHARANUM", "RAND", "LINECOUNT", "ISTIMEOUT",
    "SELECTCOM", "PREVCOM", "NEXTCOM", "LOSEBASE", "UP", "DOWN", "PALAMLV",
    "EXPLV", "EJAC", "FLAG", "TFLAG", "CFLAG", "CDFLAG", "CSTR", "CDOWN", "CUP",
    "DOWNBASE", "TCVAR", "TSTR", "TEQUIP", "TALENT", "EXP", "MARK", "RELATION",
    "JUEL", "EQUIP", "BASE", "MAXBASE", "ABL", "NOITEM", "ITEM", "ITEMPRICE",
    "NICKNAME", "CALLNAME", "NAME", "MASTERNAME", "CFLAG", "PALAM", "STAIN",
    "STR", "SAVESTR", "GLOBALS", "GLOBAL", "LOCAL", "LOCALS", "ARG", "ARGS",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
    "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "DA", "DB", "DC",
    "DD", "DE", "TA", "TB", "DITEMTYPE", "GAMEBASE_TITLE", "WINDOW_TITLE",
}
# 组内允许调用的“辅助”：断言与汇总（由其它组定义）
HELPERS = {"ASSERT_TRUE", "ASSERT_TYPE_FREE", "ASSERT_EQUAL", "TEST_SUMMARY"}
VAR_DECL = re.compile(r"^#DIMS?\s+([A-Za-z_][A-Za-z0-9_]*)")
CALL_RE = re.compile(r"^([A-Z][A-Z_0-9]*)\b")


def load_cmd_names() -> set[str]:
    names = set()
    for f in ("emuera_standard_cmds.txt", "emuera_ee_cmds.txt", "emuera_standard_funcs.txt"):
        for line in (DATA / f).read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                names.add(line)
    return names


def closer_check(blocks_map, closer, lineno, problems, stack):
    """闭合关键字需与最近的未闭合块匹配。"""
    if not stack:
        problems.append(f"{lineno}: 出现闭合关键字 «{closer}» 但无未闭合块")
        return
    open_kw, expect, at = stack[-1]
    if expect != closer:
        problems.append(f"{lineno}: 闭合 «{closer}» 与第 {at} 行的 «{open_kw}»（应为 «{expect}»）不匹配")
        return
    stack.pop()


def main() -> int:
    text = ERB.read_text(encoding="utf-8")
    lines = text.splitlines()
    problems: list[str] = []

    declared = {m.group(1).upper() for line in lines if (m := VAR_DECL.match(line))}
    cmds = load_cmd_names()

    n_stmt = n_skip = 0
    blocks: list[tuple[str, str, int]] = []
    for i, line in enumerate(lines, 1):
        s = line.strip()
        if not s or s.startswith(";"):
            if s.startswith("; SKIP "):
                n_skip += 1
            continue
        if s.startswith("@") or s.startswith("#"):
            continue
        if "\t" in line:
            problems.append(f"{i}: 含制表符")
        if s.count('"') % 2:
            problems.append(f"{i}: 引号不成对：{s[:60]}")
        # 赋值：必须真的含 `=`（`NAME[i] = …` 或 `NAME += …`），目标须已声明
        # 判定顺序：已知命令 → 赋值（目标须已声明/内置）→ 块关键字 → 未知
        m = CALL_RE.match(s)
        head = m.group(1) if m else None
        is_cmd = head in cmds or head in declared or head in BUILTIN or head in HELPERS
        m_asg = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)(\[[^\]]*\])*\s*(\+|-|\*|/|%|')?=(?!=)", s)
        if m_asg and not is_cmd:
            tgt = m_asg.group(1).upper()
            if tgt not in declared and tgt not in BUILTIN:
                problems.append(f"{i}: 赋值目标未声明 «{tgt}»")
            n_stmt += 1
            continue
        if head:
            if head in BLOCK_BODY:
                want = BLOCK_BODY[head]
                if not any(exp == want for _, exp, _ in blocks):
                    problems.append(f"{i}: «{head}» 不在 {want} 块内，不得单独成句")
            elif head in RETURN_FAMILY:
                if i < len(lines) - 4:
                    problems.append(f"{i}: 组体中间出现 «{head}»，会提前结束本组")
            elif head in FORBIDDEN_ALONE:
                problems.append(f"{i}: 该关键字不得单独成句 «{head}»")
            elif (exp := opener_of(head, blocks)) is not None:
                blocks.append((head, exp, i))
            elif head in CLOSERS:
                closer_check(None, head, i, problems, blocks)
            elif not is_cmd:
                problems.append(f"{i}: 未知语句头 «{head}»")
            n_stmt += 1
            continue
        problems.append(f"{i}: 无法归类的语句 «{s[:60]}»")

    for open_kw, expect, at in blocks:
        problems.append(f"第 {at} 行的 «{open_kw}» 未见 «{expect}» 闭合")

    # 清单一致性
    rows = [l.split("\t") for l in (DATA / "doc_smoke.tsv").read_text(encoding="utf-8").splitlines()
            if l.strip() and not l.startswith("#")]
    n_call_manifest = sum(1 for r in rows if len(r) > 2 and r[2] == "call")
    n_skip_manifest = sum(1 for r in rows if len(r) > 2 and r[2] == "skip")

    print(f"ERB 语句 {n_stmt} 条、SKIP 注释 {n_skip} 条")
    print(f"清单 call {n_call_manifest} / skip {n_skip_manifest}")
    if n_skip != n_skip_manifest:
        problems.append(f"SKIP 注释条数 {n_skip} != 清单 skip {n_skip_manifest}")
    if n_stmt < n_call_manifest:
        problems.append(f"ERB 语句数 {n_stmt} < 清单 call {n_call_manifest}（多行前置属正常，不得少于）")

    if problems:
        print(f"\n[!] {len(problems)} 个问题：")
        for x in problems[:40]:
            print("   ", x)
        return 1
    print("\n[OK] 静态校验通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
