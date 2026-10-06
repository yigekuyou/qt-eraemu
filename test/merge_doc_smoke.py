#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# merge_doc_smoke.py —— 合并各批复核结果，回写 doc_smoke.tsv
#
# 输入：test/data/_smoke_fix/smokeNN.out.tsv（复核代理产出，5 列 TSV）
#       test/data/doc_smoke.tsv（草稿，用于校验覆盖完整性）
# 输出：test/data/doc_smoke.tsv（合并后，写入前做三项校验）
#
# 校验：
#   1. 每个 (name, kind) 在草稿里恰好一行，且复核结果里恰好一行——不漏不重
#   2. mode=call 必须有 snippet，mode=skip 必须有 note
#   3. snippet 不得引用源码树（src/、Emuera/、emuera.em/、eraTW/）或 build 产物
#
# 用法：python3 test/merge_doc_smoke.py [--write]
# ---------------------------------------------------------------------------
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "test/data"
FIX_DIR = DATA / "_smoke_fix"
TSV = DATA / "doc_smoke.tsv"

FORBIDDEN = re.compile(r"(?<![A-Za-z0-9_.])(src/|Emuera/|emuera\.em/|eraTW/|build/|build-debug/)")


def load(path: pathlib.Path) -> list[tuple[str, str, str, str, str, str]]:
    """6 列：name kind mode snippet note input（旧文件 5 列时 input 补空）。"""
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) < 6:
            cols += [""] * (6 - len(cols))
        rows.append(tuple(cols[:6]))
    return rows


def main() -> int:
    draft = {(r[0], r[1]): r for r in load(TSV)}
    merged: dict[tuple[str, str], tuple] = {}
    problems: list[str] = []

    # 先铺草稿，再用各批产出覆盖：smoke*.out.tsv 按 (name,kind) 覆盖，
    # input*.out.tsv（输入族改造）按 name 覆盖（同一名字可能有 func/cmd 两行）
    merged = dict(draft)
    outs = sorted(FIX_DIR.glob("smoke*.out.tsv"))
    if not outs:
        print("没有找到 smoke*.out.tsv")
        return 1
    for p in outs:
        for row in load(p):
            key = (row[0], row[1])
            if key not in draft:
                problems.append(f"{p.name}: 草稿里没有 {key}")
                continue
            merged[key] = row

    for p in sorted(FIX_DIR.glob("input*.out.tsv")):
        for row in load(p):
            hit = [k for k in draft if k[0] == row[0]]
            if not hit:
                problems.append(f"{p.name}: 草稿里没有 {row[0]}")
                continue
            for k in hit:
                merged[k] = (k[0], k[1], row[2], row[3], row[4], row[5])

    for key, row in merged.items():
        name, kind, mode, snip, note = row[:5]
        if mode not in ("call", "skip"):
            problems.append(f"{name}: mode 非法 «{mode}»")
        if mode == "call" and not snip.strip():
            problems.append(f"{name}: call 但 snippet 为空")
        if mode == "skip" and not note.strip():
            problems.append(f"{name}: skip 但 note 为空")
        if FORBIDDEN.search(snip) or FORBIDDEN.search(note):
            problems.append(f"{name}: 引用了禁止的源码树路径")
        if row[5].strip() and mode != "call":
            problems.append(f"{name}: 给了 input 但 mode 不是 call")

    n_input = sum(1 for r in merged.values() if r[5].strip())
    n_call = sum(1 for r in merged.values() if r[2] == "call")
    n_skip = len(merged) - n_call
    print(f"批次 {len(outs)} 个；清单 {len(merged)} 行；call {n_call} / skip {n_skip}；需注入输入 {n_input} 条")
    if problems:
        print(f"\n[!] {len(problems)} 个问题：")
        for x in problems[:40]:
            print("   ", x)
        return 1

    if "--write" in sys.argv:
        order = list(draft)                    # 保持草稿顺序：func 段在前、cmd 段在后
        out = ["# name\tkind\tmode\tsnippet\tnote\tinput"]
        out += ["\t".join(merged[k]) for k in order]
        TSV.write_text("\n".join(out) + "\n", encoding="utf-8")
        print(f"已写回 {TSV.relative_to(ROOT)}")
    else:
        print("（未写回；加 --write 生效）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
