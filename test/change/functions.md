# 移植差异 · 式中函数

> 由 `test/data/functions/*.md` 的「备注」迁出（2026-10）。规则见 [`README.md`](README.md)。
> 这些函数在 C# 侧有完整语义（见各自的 `test/data/functions/<NAME>.md`），但本移植
> `src/eraengine` 未实现：调用会被解析期登记为 `expr-parse`/`arg-check` 级别的诊断，
> 运行期按「未定义的函数」宽容处理（`Operand::ast` 为空 → 求值 0/空串）。

## 未实现本函数（12 条）

`ERDNAME`、`EXISTFILE`、`EXISTMETH`、`EXISTVAR`、`GETMETH`、`GETMETHS`、`GETVAR`、`GETVARS`、
`FLOWINPUTS`、`BITMAP_CACHE_ENABLE`、`GGETBRUSH`、`GGETFONTSTYLE`

## 未实现本函数族（32 条）

- **`DT_*`（DataTable，21 条）**：`DT_CREATE`、`DT_EXIST`、`DT_RELEASE`、`DT_CLEAR`、`DT_NOCASE`、
  `DT_FROMXML`、`DT_TOXML`、`DT_SELECT`、`DT_CELL_GET`、`DT_CELL_GETS`、`DT_CELL_SET`、`DT_CELL_ISNULL`、
  `DT_COLUMN_ADD`、`DT_COLUMN_EXIST`、`DT_COLUMN_LENGTH`、`DT_COLUMN_NAMES`、`DT_COLUMN_REMOVE`、
  `DT_ROW_ADD`、`DT_ROW_SET`、`DT_ROW_REMOVE`、`DT_ROW_LENGTH`
  （另命令形态 `DT_COLUMN_OPTIONS` 见 [`commands.md`](commands.md)）
- **`ENUM*`（10 条）**：`ENUMFILES`、`ENUMFUNCBEGINSWITH`、`ENUMFUNCENDSWITH`、`ENUMFUNCWITH`、
  `ENUMMACROBEGINSWITH`、`ENUMMACROENDSWITH`、`ENUMMACROWITH`、`ENUMVARBEGINSWITH`、`ENUMVARENDSWITH`、
  `ENUMVARWITH`

## 其它（同名/同族差异）

- `ARRAYMSORTEX`：未实现本函数族（C# 侧为 EM 私家版追加，语义见
  `test/data/functions/ARRAYMSORTEX.md`）。
- `CSVCFLAG`/`CSVEQUIP`/`CSVEXP`/`CSVJUEL`/`CSVMARK`/`CSVRELATION`/`CSVTALENT`：本移植已有实现路径，
  但 example 的 `Chara0.csv` 未建对应模板数组，冒烟里按 `arg-check`/越界提示（见
  `test/data/doc_smoke.tsv` 的对应行）。
- `FIND_VARDATA`/`CHKVARDATA`/`CHKGLOBALDATA`：**已实现**（EE 存档系，见
  `src/eraengine/GameProc/ee_extension.cpp` 的 `ExtensionRegistry` 登记）。
