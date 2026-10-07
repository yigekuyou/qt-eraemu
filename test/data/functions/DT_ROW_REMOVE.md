# DT_ROW_REMOVE

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_ROW_REMOVE(str 数据表名, int id)
- **签名**：int DT_ROW_REMOVE(str 数据表名, ref int id数组, int 个数)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义由源码得出

## 语义

按 `id` 删除数据表中的一行或一批行。

- 表不存在 → `-1`（两种形态都一样）。
- **单行形态** `DT_ROW_REMOVE(表名, id)`：若存在该 `id` 的行则删除并返回 `1`；找不到返回 `0`。
- **批量形态** `DT_ROW_REMOVE(表名, id数组, 个数)`：取整数数组的前 `个数` 个元素（`个数` 超过数组长度时按数组长度算，即 `min(个数, 数组长度)`），拼成 `id IN (a,b,c)` 过滤表达式交给 `DataTable.Select` 选出命中行后逐行删除，返回**实际删除的行数**。
  - `个数 <= 0` → 立即返回 `0`（不删任何行）；
  - 数组里的 id 不存在时只是选不中，不报错，返回真实删除数（可能为 0）；
  - 由于使用 `Select` + `id IN (...)`，被删行按 .NET 过滤语义匹配：若表里存在非整数 id 列无法比较等情况会由 .NET 抛异常（推定）。
- 删除后表的列结构不变，`DT_ROW_LENGTH` 减少；`id` 不复用。
- 与 `DT_SELECT` 的配合：先用 `DT_SELECT` 取出一批 `id` 填入整数数组，再交给本函数的批量形态可实现条件删除。

## 用法

### int DT_ROW_REMOVE(str 数据表名, int id)
```erb
#DIM rid
DT_CREATE "t"
DT_COLUMN_ADD "t", "lv", "int32"
rid = DT_ROW_ADD "t", "lv", 5
PRINTFORML {DT_ROW_REMOVE("t", rid)}        ;→ 1
PRINTFORML {DT_ROW_REMOVE("t", rid)}        ;→ 0（已删除）
PRINTFORML {DT_ROW_REMOVE("no_such", 1)}    ;→ -1
```

### int DT_ROW_REMOVE(str 数据表名, ref int id数组, int 个数)
```erb
#DIM IDS, 100            ; 一维整数数组（批量形态要用整数数组）
#DIM n
DT_ROW_ADD "t", "lv", 1
DT_ROW_ADD "t", "lv", 2
n = DT_SELECT("t", "lv <= 2")          ; 取命中行数，id 填入 RESULT
IDS:0 = RESULT:1
IDS:1 = RESULT:2
PRINTFORML {DT_ROW_REMOVE("t", IDS, n)}   ;→ 2（删除这两行）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:295`（`["DT_ROW_REMOVE"] = new DataTableRowRemoveMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1531`（`DataTableRowRemoveMethod`）

```text
构造（Creator.Method.cs:1533-1541）:
    argumentTypeArrayEx = [
        { String, Int }                            ; 单行形态（恰好 2 参）
        { String, RefInt1D, Int }                  ; 批量形态（恰好 3 参）
    ]
    返回类型 = long；CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1542-1567）:
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return -1
    dt = dict[key]
    if args.Count == 3:                            ; 批量
        array = (args[1] as VariableTerm) 的整数数组
        count = min((int)args[2], array.Length)
        if count <= 0: return 0
        sb = "(" + array[0] + "," + array[1] + ... + ")"     ; 前 count 个
        rows = dt.Select("id IN " + sb)            ; System.Data 过滤
        if rows == null: return 0
    else:                                          ; 单行
        row = dt.Rows.Find(args[1].GetIntValue(exm))
        if row == null: return 0
        rows = [row]
    foreach row in rows: dt.Rows.Remove(row)
    return rows.Length                             ; 实际删除的行数
```

## 备注

- 无既有文档可对照。
- 批量形态返回的是**删除数**（`rows.Length`），单行形态返回 `1`/`0`，两者语义一致但含义不同：批量形态即便只删到 1 行也返回 1，不会返回 `-2` 之类的「不存在」码。
- 「第一个数组元素」的处理有个小瑕疵：拼串时第 0 个元素直接 `array[0].ToString()`，其余加逗号前缀（`Runtime/Script/Statements/Function/Creator.Method.cs:1556-1557`），拼出的字符串语法正确，但**不做参数注入防护**——`id` 是整数数组，注入风险仅存在于整数范围（推定无实际风险）。
- `dt.Select("id IN (...)")` 依赖 `id` 列名与主键定义，故只对 `DT_CREATE` 建立的表（或含 `id` 列的 `DT_FROMXML` 表）有效；表若被 `DT_COLUMN_REMOVE` 之外的手段改名则不能删除（`id` 本就不可删）。
