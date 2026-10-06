# DT_CELL_ISNULL

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_CELL_ISNULL(str 数据表名, int 行, str 列名{, int 按id查找})
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

判定单元格是否为 NULL（.NET `DBNull`），是这套函数中**唯一会区分错误态**的读取函数。与 `DT_CELL_GET`/`DT_CELL_GETS` 共用实现类 `DataTableCellGetMethod`（`Operation.IsNull`）。

返回码（`Runtime/Script/Statements/Function/Creator.Method.cs:1582-1608`）：

| 返回值 | 含义 |
|---|---|
| `1` | 定位成功，该单元格是 NULL |
| `0` | 定位成功，该单元格有值 |
| `-1` | 数据表不存在 |
| `-2` | 表存在，但行或列不存在 |

- 定位方式：第 4 参数省略或 0 → 第 2 参数按**行序号**；非 0 → 第 2 参数按**行 id**（主键查找）。
- 「有值」包括空字符串（`""` 不是 DBNull，返回 0）；只有从未赋值/显式写 NULL 的格子才是 `1`。
- 通过 `DT_ROW_ADD` 时留空值、`DT_CELL_SET` 省略值等操作写入的正是 `DBNull`。

## 用法

### int DT_CELL_ISNULL(str 数据表名, int 行, str 列名{, int 按id查找})
- 数据表名 / 列名：字符串表达式。
- 行：行序号或行 id（取决于第 4 参数）。
- 按id查找：省略或 0 → 行序号；非 0 → 按 id。
- 返回值：`1` / `0` / `-1` / `-2`。
```erb
DT_CREATE "t"
DT_COLUMN_ADD "t", "name", "string"
DT_ROW_ADD "t"                          ; name 未赋值 → NULL
DT_ROW_ADD "t", "name", "甲"
PRINTFORML {DT_CELL_ISNULL("t", 0, "name")}          ;→ 1（NULL）
PRINTFORML {DT_CELL_ISNULL("t", 1, "name")}          ;→ 0（有值）
PRINTFORML {DT_CELL_ISNULL("t", 9, "name")}          ;→ -2（行不存在）
PRINTFORML {DT_CELL_ISNULL("t", 0, "nocol")}         ;→ -2（列不存在）
PRINTFORML {DT_CELL_ISNULL("no_such", 0, "name")}    ;→ -1（表不存在）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:299`（`["DT_CELL_ISNULL"] = new DataTableCellGetMethod(DataTableCellGetMethod.Operation.IsNull)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1569`（`DataTableCellGetMethod`）

```text
构造（Creator.Method.cs:1574-1577）:
    返回类型 = long（Get / IsNull 都是整数返回）
    argumentTypeArrayEx = [{ String, Int, String, Int }, OmitStart = 3]
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1582-1608，op == IsNull 分支）:
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return -1                      ; 表不存在
    asId = (args.Count == 4) ? (args[3] != 0) : false
    dt   = dict[key]
    idx  = args[1].GetIntValue(exm)
    name = args[2] 的字符串值
    if asId:
        row = dt.Rows.Find(idx)
        if row != null && dt.Columns.Contains(name):
            return (row[name] == DBNull) ? 1 : 0
    else:
        if 0 <= idx && idx < dt.Rows.Count && dt.Columns.Contains(name):
            return (dt.Rows[(int)idx][name] == DBNull) ? 1 : 0
    return -2                                                 ; 行或列不存在
```

## 备注

- 无既有文档可对照。
- 本函数是判断「单元格取不到值」的正确手段：`DT_CELL_GET` 把 NULL 与 0 混同、`DT_CELL_GETS` 把 NULL 与空串混同。
- 列被 `DT_COLUMN_ADD` 追加到已有行上时，旧行在该列上都是 NULL（推定：.NET 行为），此时本函数返回 1。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
