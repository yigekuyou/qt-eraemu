# DT_COLUMN_EXIST

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_COLUMN_EXIST(str 数据表名, str 列名)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

查询表内是否存在指定列，并**同时返回该列的数据类型编号**：

| 返回值 | 含义 |
|---|---|
| `1` | 存在，类型 int8（sbyte） |
| `2` | 存在，类型 int16（short） |
| `3` | 存在，类型 int32（int） |
| `4` | 存在，类型 int64（long） |
| `5` | 存在，类型 string |
| `0` | 表存在但没有这一列 |
| `-1` | 数据表本身不存在 |
| `long.MaxValue` | 列存在但类型不在内置 5 种之内（`TypeToInt` 的兜底，理论上不可达，推定） |

- 判定用的是 .NET `DataColumnCollection.Contains` / 索引器，列名比较是否区分大小写取决于该表 `CaseSensitive`（`DT_CREATE` 后为区分；`DT_NOCASE` 可改）——推定：源码只调用 .NET API。
- 类型编号与 `DT_COLUMN_ADD` 接受的关键字/编号一一对应（1=int8 … 5=string）。
- 纯查询，无副作用。

## 用法

### int DT_COLUMN_EXIST(str 数据表名, str 列名)
- 数据表名 / 列名：字符串表达式。
- 返回值：如上的类型编号 / `0` / `-1`。
```erb
DT_CREATE "t"
DT_COLUMN_ADD "t", "lv", "int64"
PRINTFORML {DT_COLUMN_EXIST("t", "id")}     ;→ 4（自动主键列 id 是 long）
PRINTFORML {DT_COLUMN_EXIST("t", "lv")}     ;→ 4
PRINTFORML {DT_COLUMN_EXIST("t", "lv2")}    ;→ 0
PRINTFORML {DT_COLUMN_EXIST("no", "lv")}    ;→ -1
IF DT_COLUMN_EXIST("t", "lv") == 0
	DT_COLUMN_ADD "t", "lv", "int64"
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:289`（`["DT_COLUMN_EXIST"] = new DataTableColumnManagementMethod(DataTableColumnManagementMethod.Operation.Check)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1325`（`DataTableColumnManagementMethod`）
- 类型编号：`Runtime/Utils/EvilMask/Utils.cs:299`（`TypeToInt`）

```text
构造（Creator.Method.cs:1339-1340）:
    op != Create / Names → argumentTypeArray = [String, String]（两参必填）
    返回类型 = long；CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1345-1350, 1359-1363）:
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return -1
    dt = dict[key]
    cName = args[1] 的字符串值
    contains = dt.Columns.Contains(cName)
    case Check:
        return contains ? Utils.DataTable.TypeToInt(dt.Columns[cName].DataType)
                        : 0
        ; TypeToInt：sbyte→1, short→2, int→3, long→4, string→5，其它→long.MaxValue
```

## 备注

- 无既有文档可对照。
- 与 `EXISTVAR`（同为 EM 私家版）的返回风格相似：都是「位/编号编码 + 0 表示不存在」，差别是 `EXISTVAR` 找不到变量返回 0 而不区分容器不存在。
- 注意返回值 `1..5` 与「真(true)=1」不同：想知道「列是否存在」应写 `DT_COLUMN_EXIST(...) != 0`，且要先排除 `-1`（表不存在）。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
