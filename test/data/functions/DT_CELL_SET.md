# DT_CELL_SET

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_CELL_SET(str 数据表名, int 行, str 列名{, 值{, int 按id查找}})
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

改写一个单元格的值，成功返回 `1`。返回码（`Runtime/Script/Statements/Function/Creator.Method.cs:1647-1677`）：

| 返回值 | 含义 |
|---|---|
| `1` | 成功写入 |
| `-1` | 数据表不存在 |
| `0` | 列名是 `id`（大小写不敏感比较 `name.ToLower() == "id"`，禁止改写主键） |
| `-2` | 值的类型与列类型不符（字符串列给整数 / 数值列给字符串） |
| `-3` | 表存在、但行或列不存在 |

- 定位方式：第 5 参数省略或 0 → 第 2 参数按**行序号**（`0 <= 行 < DT_ROW_LENGTH`）；非 0 → 第 2 参数按**行 id**（主键查找）。
- 第 4 参数（值）可省略：省略时写入 `DBNull`（把格子置空）。也可以显式留空：`DT_CELL_SET("t", 0, "name", )` 同样得到 NULL。
- 值类型必须与列类型一致：字符串列 → 字符串表达式；数值列 → 整数表达式。数值写入会按列类型裁剪（`Utils.DataTable.ConvertInt`，`Runtime/Utils/EvilMask/Utils.cs:323`：int8/int16/int32 夹到边界，int64 原样）。
- 与 `DT_ROW_SET` 的分工：本函数按「行序号 **或** id」单点定位、一次只写一列；`DT_ROW_SET` 只按 id、可一次写多列。

## 用法

### int DT_CELL_SET(str 数据表名, int 行, str 列名, 值{, int 按id查找})
```erb
#DIM rid
DT_CREATE "t"
DT_COLUMN_ADD "t", "name", "string"
DT_COLUMN_ADD "t", "lv", "int32"
rid = DT_ROW_ADD "t", "name", "甲", "lv", 10

PRINTFORML {DT_CELL_SET("t", 0, "name", "乙")}          ;→ 1（按行序号）
PRINTFORML {DT_CELL_SET("t", rid, "lv", 99, 1)}         ;→ 1（按 id）
PRINTFORML {DT_CELL_SET("t", 0, "id", 1)}               ;→ 0（id 不可改）
PRINTFORML {DT_CELL_SET("t", 0, "lv", "x")}             ;→ -2（类型不符）
PRINTFORML {DT_CELL_SET("t", 9, "lv", 1)}               ;→ -3（行不存在）
PRINTFORML {DT_CELL_SET("no_such", 0, "lv", 1)}         ;→ -1（表不存在）
PRINTFORML {DT_CELL_GETS("t", 0, "name")}               ;→ 乙
```

### 置为 NULL（省略值）
```erb
DT_CELL_SET "t", 0, "name"        ; 值省略 → DBNull
PRINTFORML {DT_CELL_ISNULL("t", 0, "name")}   ;→ 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:301`（`["DT_CELL_SET"] = new DataTableCellSetMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1637`（`DataTableCellSetMethod`）

```text
构造（Creator.Method.cs:1639-1646）:
    返回类型 = long
    argumentTypeArrayEx = [{ String, Int, String, Any, Int }, OmitStart = 3]
    ; 表名、行、列名必填；值（第 4）与按id（第 5）可省
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1647-1677）:
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return -1
    asId = (args.Count == 5) ? (args[4] != 0) : false
    dt   = dict[key]
    idx  = args[1].GetIntValue(exm)
    name = args[2] 的字符串值
    if name.ToLower() == "id": return 0                   ; 主键列禁止改写
    v = (args.Count > 3) ? args[3] : null
    row = null
    if asId: row = dt.Rows.Find(idx)
    else if 0 <= idx && idx < dt.Rows.Count: row = dt.Rows[(int)idx]
    if row != null && dt.Columns.Contains(name):
        if v == null: row[name] = DBNull.Value            ; 省略值 → 置 NULL
        else:
            isString = (列类型 == typeof(string))
            if v.GetOperandType() != (isString ? string : long): return -2
            if isString: row[name] = v.GetStrValue(exm)
            else:        row[name] = Utils.DataTable.ConvertInt(v.GetIntValue(exm), 列类型)
        return 1
    return -3                                             ; 行或列不存在
```

## 备注

- 无既有文档可对照。
- 与 `DT_CELL_GET/GETS` 的定位参数位置一致（按 id 标志固定在末位），但本函数的「值」插在第 4 位，写作 `DT_CELL_SET("t", id, "col", v, 1)` 时 `1` 是「按 id」而不是值。
- `id` 判定用 `ToLower()`，因此 `ID`/`Id` 也返回 0（即使表区分大小写、且确实存在另一个名为 `ID` 的用户列——那种情况下 `ID` 列会被误判为不可写：源码先判名字再查列，这是实现上的一个副作用，推定）。
- 类型不符返回 `-2`（不是抛 CodeEE），与 `DT_ROW_SET`/`DT_ROW_ADD` 的 `SetValue` 抛 CodeEE 的行为**不同**——同一族里两种错误风格并存。
