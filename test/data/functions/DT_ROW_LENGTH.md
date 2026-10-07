# DT_ROW_LENGTH

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_ROW_LENGTH(str 数据表名)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

返回指定数据表的**行数**；表不存在返回 `-1`。

- 刚 `DT_CREATE` 的表是 0 行；`DT_ROW_ADD` +1，`DT_ROW_REMOVE` 按删除数减少，`DT_CLEAR` 归 0；`DT_COLUMN_*` 不影响行数。
- 纯查询，无副作用。与 `DT_COLUMN_LENGTH` 共用实现类 `DataTableLengthMethod`（`Operation.Row` / `Operation.Column` 分支）。

## 用法

### int DT_ROW_LENGTH(str 数据表名)
- 数据表名：字符串表达式。
- 返回值：行数；表不存在 `-1`。
```erb
DT_CREATE "t"
PRINTFORML {DT_ROW_LENGTH("t")}          ;→ 0
DT_ROW_ADD "t"
DT_ROW_ADD "t"
PRINTFORML {DT_ROW_LENGTH("t")}          ;→ 2
DT_CLEAR "t"
PRINTFORML {DT_ROW_LENGTH("t")}          ;→ 0
PRINTFORML {DT_ROW_LENGTH("no_such")}    ;→ -1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:296`（`["DT_ROW_LENGTH"] = new DataTableLengthMethod(DataTableLengthMethod.Operation.Row)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1512`（`DataTableLengthMethod`）

```text
构造（Creator.Method.cs:1516-1521）:
    返回类型 = long；参数 = [String]（必填 1 个）；CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1523-1529）:
    key = args[0] 的字符串值
    dict = exm.VEvaluator.VariableData.DataDataTables
    if !dict.ContainsKey(key): return -1
    return (op == Row) ? dict[key].Rows.Count : dict[key].Columns.Count
    ; DT_ROW_LENGTH → Rows.Count；DT_COLUMN_LENGTH → Columns.Count
```

## 备注

- 无既有文档可对照。
- 与 `MAP_SIZE`（`Emuera.EM_readme.txt:248`，返回键值对数量）对应：都是「容器元素计数 + 不存在返回 -1」。
