# DT_CELL_GETS

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：str DT_CELL_GETS(str 数据表名, int 行, str 列名{, int 按id查找})
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

读取一个单元格的**字符串值**（Gets = Get String），是 `DT_CELL_GET` 的字符串返回版，两者共用实现类 `DataTableCellGetMethod`（`Operation.Gets`）。

返回规则：

| 情况 | 返回值 |
|---|---|
| 表不存在 | `""` |
| 定位成功且值非 NULL | 值的字符串形式 |
| 定位成功但值为 NULL（DBNull） | `""` |
| 行或列不存在 | `""` |

- 定位方式同 `DT_CELL_GET`：第 4 参数省略或 0 → 第 2 参数按**行序号**；非 0 → 第 2 参数按**行 id**（`Rows.Find` 主键查找）。
- **源码内部存在两条取值路径的差异**（`Runtime/Script/Statements/Function/Creator.Method.cs:1609-1635`）：
  - 行序号路径用 `v.ToString()` 转字符串，数值列（int8..int64）也能读出十进制字符串；
  - 按 id 路径用 `(string)v` **强制类型转换**，数值列会抛 `InvalidCastException`（非 CodeEE）。
  即：对数值列使用按 id 形态读取字符串带崩溃风险（推定：源码原样如此，属实现缺陷）。
- 因所有失败态都返回空字符串，无法区分「值就是空串」与「取不到」；需要判定请用 `DT_CELL_ISNULL` / `DT_EXIST` / `DT_COLUMN_EXIST`。

## 用法

### str DT_CELL_GETS(str 数据表名, int 行, str 列名{, int 按id查找})
- 数据表名 / 列名：字符串表达式。
- 行：行序号或行 id（取决于第 4 参数）。
- 按id查找：省略或 0 → 行序号；非 0 → 按 id。
- 返回值：单元格字符串；不存在 / NULL → `""`。
```erb
#DIM rid
DT_CREATE "t"
DT_COLUMN_ADD "t", "name", "string"
rid = DT_ROW_ADD "t", "name", "甲"
PRINTFORML [{DT_CELL_GETS("t", 0, "name")}]          ;→ [甲]
PRINTFORML [{DT_CELL_GETS("t", rid, "name", 1)}]     ;→ [甲]
PRINTFORML [{DT_CELL_GETS("t", 9, "name")}]          ;→ []（行不存在）
PRINTFORML [{DT_CELL_GETS("no_such", 0, "name")}]    ;→ []（表不存在）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:300`（`["DT_CELL_GETS"] = new DataTableCellGetMethod(DataTableCellGetMethod.Operation.Gets)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1569`（`DataTableCellGetMethod`）

```text
构造（Creator.Method.cs:1574-1577）:
    返回类型 = (op == Gets) ? typeof(string) : typeof(long)   ; Gets → 字符串
    argumentTypeArrayEx = [{ String, Int, String, Int }, OmitStart = 3]
    CanRestructure = false

GetStrValue(exm, args)（Creator.Method.cs:1609-1635）:
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return ""
    asId = (args.Count == 4) ? (args[3] != 0) : false
    dt   = dict[key]
    idx  = args[1].GetIntValue(exm)
    name = args[2] 的字符串值
    if asId:
        row = dt.Rows.Find(idx)
        if row != null && dt.Columns.Contains(name):
            v = row[name]
            if v != DBNull: return (string)v        ; ← 强转：数值列会抛 InvalidCastException
    else:
        if 0 <= idx && idx < dt.Rows.Count && dt.Columns.Contains(name):
            v = dt.Rows[(int)idx][name]
            if v != DBNull: return v.ToString()     ; ← ToString：数值列可读出
    return ""
```

## 备注

- 无既有文档可对照。
- 与 `DT_CELL_GET` 的对照：整数版对 NULL 返回 0、对数值列必然可用；字符串版对 NULL 返回空串、且**按 id 形态对数值列不安全**。取数值列请用 `DT_CELL_GET`。
- 返回空串的失败态过多（表缺失、行列缺失、NULL、空串本身），属于「宽容返回」风格，与 `MAP_GET`（`Emuera.EM_readme.txt:229`：不存在也返回空字符串，建议用 `MAP_HAS` 确认）一致。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
