# DT_CLEAR

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_CLEAR(str 数据表名)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

删除指定数据表的**全部行**（保留列定义与主键约束），成功返回 `1`；表不存在返回 `-1`。

- 相当于 .NET `DataTable.Clear()`：行全删、`DT_ROW_LENGTH` 归 0，但 `DT_COLUMN_LENGTH` 与列类型、`DT_COLUMN_NAMES` 结果不变，表本身仍存在（`DT_EXIST` 仍为 1）。
- 主键 `id` 仍然是 `id` 列，`DT_ROW_ADD` 之后可继续正常追加新行（id 由时间点重新生成，不复用旧值）。
- 与 `DT_RELEASE` 的区别：本函数保留表对象，只清行。
- 清档等系统流程（`RemoveEMSaveData` 等，`Runtime/Script/Statements/Variable/VariableData.cs:1089-1160`）内部也调用同样的 `Clear()`，但那不是本函数。

## 用法

### int DT_CLEAR(str 数据表名)
- 数据表名：字符串表达式。
- 返回值：成功 `1`；表不存在 `-1`。
```erb
DT_CREATE "log"
DT_COLUMN_ADD "log", "text", "string"
DT_ROW_ADD "log", "text", "第一条"
DT_ROW_ADD "log", "text", "第二条"
PRINTFORML {DT_ROW_LENGTH("log")}       ;→ 2
DT_CLEAR "log"
PRINTFORML {DT_ROW_LENGTH("log")}       ;→ 0
PRINTFORML {DT_COLUMN_LENGTH("log")}    ;→ 2（id 与 text 列仍在）
PRINTFORML {DT_CLEAR("no_such_table")}  ;→ -1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:285`（`["DT_CLEAR"] = new DataTableManagementMethod(DataTableManagementMethod.Operation.Clear)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1270`（`DataTableManagementMethod`）

```text
构造（Creator.Method.cs:1277-1282）:
    返回类型 = long；参数 = [String]；CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1284-1299）:
    key  = args[0] 的字符串值
    dict = exm.VEvaluator.VariableData.DataDataTables
    case Clear:
        if dict.ContainsKey(key):
            dict[key].Clear()      ; System.Data.DataTable.Clear()：删所有行，保留列与主键
            return 1
        return -1
```

## 备注

- 无既有文档可对照。
- 与 `MAP_CLEAR`（`Emuera.EM_readme.txt:252`）行为差异明显：`MAP_CLEAR` 清空键值对（等价于删全部元素）而 DT 保留列结构；返回码 -1（表不存在）两者一致。
- `Clear()` 不会重置 `CaseSensitive`、列默认值（`DT_COLUMN_OPTIONS` 设定的 `DefaultValue`）等列属性。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
