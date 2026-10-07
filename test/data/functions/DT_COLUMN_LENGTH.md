# DT_COLUMN_LENGTH

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_COLUMN_LENGTH(str 数据表名)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

返回指定数据表的**列数**（包含 `DT_CREATE` 自动创建的 `id` 列）；表不存在返回 `-1`。

- 空表（无行）也有列数：刚 `DT_CREATE` 完是 `1`（只有 `id`）。
- `DT_COLUMN_ADD` 成功后 +1，`DT_COLUMN_REMOVE` 成功后 -1；`DT_CLEAR` 不影响列数。
- 纯查询。与 `DT_ROW_LENGTH` 共用实现类 `DataTableLengthMethod`（按 `Operation.Row/Column` 分支）。

## 用法

### int DT_COLUMN_LENGTH(str 数据表名)
- 数据表名：字符串表达式。
- 返回值：列数；表不存在 `-1`。
```erb
DT_CREATE "t"
PRINTFORML {DT_COLUMN_LENGTH("t")}         ;→ 1（只有 id）
DT_COLUMN_ADD "t", "a", "int32"
DT_COLUMN_ADD "t", "b", "string"
DT_ROW_ADD "t", "a", 1, "b", "x"
PRINTFORML {DT_COLUMN_LENGTH("t")}         ;→ 3
PRINTFORML {DT_COLUMN_LENGTH("no_such")}   ;→ -1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:291`（`["DT_COLUMN_LENGTH"] = new DataTableLengthMethod(DataTableLengthMethod.Operation.Column)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1512`（`DataTableLengthMethod`）

```text
构造（Creator.Method.cs:1516-1521）:
    返回类型 = long；参数 = [String]（必填 1 个）；CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1523-1529）:
    key = args[0] 的字符串值
    dict = exm.VEvaluator.VariableData.DataDataTables
    if !dict.ContainsKey(key): return -1
    return (op == Column) ? dict[key].Columns.Count : dict[key].Rows.Count
    ; DT_COLUMN_LENGTH → Columns.Count；DT_ROW_LENGTH → Rows.Count
```

## 备注

- 无既有文档可对照。
- 列数永远 ≥ 1（`id` 列无法删除，`DT_COLUMN_REMOVE` 对 `id` 返回 0），所以本函数不会返回 0（表存在时）。
