# DT_COLUMN_REMOVE

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_COLUMN_REMOVE(str 数据表名, str 列名)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

删除指定数据表的一列（连同该列在所有行上的数据），成功返回 `1`。

- 表不存在 → `-1`；
- 表存在但无此列 → `0`；
- 列名是自动主键列 `id`（**大小写不敏感**比较，源码 `cName.ToLower() != "id"`）→ `0`，即 `id` 不可删除。
- 大小写不敏感这一条是本函数特有：`DT_NOCASE` 无论怎么设置，都删不掉 `id`/`ID`/`Id` 这类名字的列（其它名字的比较仍受表的 `CaseSensitive` 影响——推定：源码只对该名字做 `ToLower()` 判断，其余交给 .NET API）。
- 删除列后：`DT_COLUMN_LENGTH` -1、`DT_COLUMN_NAMES` 结果变化、已有行数据随之丢弃；`DT_CELL_GET` 等按该列名的访问会变成「列不存在」分支（返回 0 / -3 / -2 依函数而异）。
- 删除的列无法恢复（没有「改类型」的替代手段，只能重新 `DT_COLUMN_ADD`）。

## 用法

### int DT_COLUMN_REMOVE(str 数据表名, str 列名)
- 数据表名 / 列名：字符串表达式。
- 返回值：成功 `1`；无此列或为 `id` → `0`；表不存在 `-1`。
```erb
DT_CREATE "t"
DT_COLUMN_ADD "t", "tmp", "int32"
PRINTFORML {DT_COLUMN_REMOVE("t", "tmp")}    ;→ 1
PRINTFORML {DT_COLUMN_REMOVE("t", "tmp")}    ;→ 0（已删除）
PRINTFORML {DT_COLUMN_REMOVE("t", "ID")}     ;→ 0（id 列大小写不敏感地受保护）
PRINTFORML {DT_COLUMN_REMOVE("no_such", "x")};→ -1
PRINTFORML {DT_COLUMN_LENGTH("t")}           ;→ 1（只剩 id）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:290`（`["DT_COLUMN_REMOVE"] = new DataTableColumnManagementMethod(DataTableColumnManagementMethod.Operation.Remove)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1325`（`DataTableColumnManagementMethod`）

```text
构造（Creator.Method.cs:1339-1340）:
    op == Remove → argumentTypeArray = [String, String]（两参必填）
    返回类型 = long；CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1345-1350, 1359-1372）:
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return -1
    dt = dict[key]
    cName = args[1] 的字符串值
    contains = dt.Columns.Contains(cName)
    case Remove:
        if contains && cName.ToLower() != "id":     ; id 列受保护（忽略大小写）
            dt.Columns.Remove(cName)
            return 1
        return 0
```

## 备注

- 无既有文档可对照。
- `id` 保护用 `ToLower()` 而非 `ToUpper()`，对英文字母效果相同；该判断不受 `CaseSensitive` 影响，因此 `DT_NOCASE "t", 1` 也删不掉大小写变体的 `id`。
- 删除列会把该列在全部行上的值一并丢弃（System.Data 语义），不存在的别名列不会报错，只返回 0。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
