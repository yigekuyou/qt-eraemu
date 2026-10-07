# DT_CELL_GET

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_CELL_GET(str 数据表名, int 行, str 列名{, int 按id查找})
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

读取一个单元格的**整数值**。与 `DT_CELL_GETS`（字符串版）、`DT_CELL_ISNULL`（空值判定）共用实现类 `DataTableCellGetMethod`。

返回规则（`Runtime/Script/Statements/Function/Creator.Method.cs:1582-1608`）：

| 情况 | 返回值 |
|---|---|
| 表不存在 | `0` |
| 定位成功且值非 NULL | 该值的整数（`Convert.ToInt64`） |
| 定位成功但值为 NULL（DBNull） | `0` |
| 行或列不存在 | `0` |

- 定位方式由第 4 参数（可省略，非 0 生效）决定：
  - 省略 / 0：第 2 参数是**行序号**（0 起，须满足 `0 <= 行 < DT_ROW_LENGTH`）；
  - 非 0：第 2 参数是**行的 id**（主键值），用 `Rows.Find` 查找。
- 值按 .NET `Convert.ToInt64` 转换，因此**字符串列**里形如 `"12"` 的内容也能取出整数 12；非数字字符串会抛 `FormatException`（非 CodeEE，属未捕获异常，推定）。
- 因为「表不存在」与「值恰为 0」返回相同的 `0`，需要区分时先用 `DT_EXIST` / `DT_CELL_ISNULL`。

## 用法

### int DT_CELL_GET(str 数据表名, int 行, str 列名{, int 按id查找})
- 数据表名 / 列名：字符串表达式。
- 行：行序号（省略第 4 参数时）或行 id（第 4 参数非 0 时）。
- 按id查找：省略或 0 → 行序号；非 0 → 按 id。
- 返回值：单元格整数值；不存在 / NULL → `0`。
```erb
#DIM rid
DT_CREATE "t"
DT_COLUMN_ADD "t", "lv", "int32"
rid = DT_ROW_ADD "t", "lv", 42
PRINTFORML {DT_CELL_GET("t", 0, "lv")}          ;→ 42（按行序号）
PRINTFORML {DT_CELL_GET("t", rid, "lv", 1)}     ;→ 42（按 id）
PRINTFORML {DT_CELL_GET("t", 9, "lv")}          ;→ 0（行不存在）
PRINTFORML {DT_CELL_GET("no_such", 0, "lv")}    ;→ 0（表不存在）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:298`（`["DT_CELL_GET"] = new DataTableCellGetMethod(DataTableCellGetMethod.Operation.Get)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1569`（`DataTableCellGetMethod`；同族 `DT_CELL_ISNULL` 于 `:299`、`DT_CELL_GETS` 于 `:300`）

```text
构造（Creator.Method.cs:1572-1580）:
    返回类型 = (op == Gets) ? typeof(string) : typeof(long)
    argumentTypeArrayEx = [{ String, Int, String, Int }, OmitStart = 3]
    ; 表名、行、列名必填；第 4 参（按id）可省
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1582-1608）:
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return (op == IsNull) ? -1 : 0
    asId = (args.Count == 4) ? (args[3] != 0) : false
    dt   = dict[key]
    idx  = args[1].GetIntValue(exm)
    name = args[2] 的字符串值
    if asId:
        row = dt.Rows.Find(idx)
        if row != null && dt.Columns.Contains(name):
            v = row[name]
            if op == Get:    return (v == DBNull) ? 0 : Convert.ToInt64(v)
            else:            return (v == DBNull) ? 1 : 0        ; IsNull
    else:
        if 0 <= idx && idx < dt.Rows.Count && dt.Columns.Contains(name):
            v = dt.Rows[(int)idx][name]
            if op == Get:    return (v == DBNull) ? 0 : Convert.ToInt64(v)
            else:            return (v == DBNull) ? 1 : 0
    return (op == IsNull) ? -2 : 0        ; 行/列不存在
```

## 备注

- 无既有文档可对照。
- 「值恰为 0」「值为 NULL」「行/列不存在」「表不存在」在 `DT_CELL_GET` 下都是 `0`，无法区分；需要精确判定请用 `DT_CELL_ISNULL`（错误态会用 -1/-2 表达）。
- 字符串列也能取整数（`Convert.ToInt64`），且非数字内容会抛 .NET 异常而非 CodeEE → 实际使用中建议对字符串列统一用 `DT_CELL_GETS`。
