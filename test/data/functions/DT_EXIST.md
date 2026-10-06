# DT_EXIST

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_EXIST(str 数据表名)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

查询名为 `<数据表名>` 的数据表是否存在：存在返回 `1`，不存在返回 `0`。

纯查询，不创建、不修改任何表。与 `DT_CREATE`（已存在时返回 0）、`DT_RELEASE`（删除）、`DT_CLEAR`（清空行）是同一实现类 `DataTableManagementMethod` 的不同 `Operation`。

`DT_EXIST` 是这套函数里唯一「表不存在」不算异常、也不返回负数的查询：其余多数函数在表不存在时返回 `-1`，本函数返回 `0`。

## 用法

### int DT_EXIST(str 数据表名)
- 数据表名：字符串表达式（大小写敏感，与 `DT_CREATE` 传入的键完全一致才算命中）。
- 返回值：存在 `1`；不存在 `0`。
```erb
IF DT_EXIST("玩家表") == 0
	DT_CREATE "玩家表"
ENDIF
DT_RELEASE "玩家表"
PRINTFORML {DT_EXIST("玩家表")}   ;→ 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:281`（`["DT_EXIST"] = new DataTableManagementMethod(DataTableManagementMethod.Operation.Check)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1270`（`DataTableManagementMethod`）
- 表的存储：`Runtime/Script/Statements/Variable/VariableData.cs:21`

```text
构造（Creator.Method.cs:1277-1282）:
    返回类型 = long；参数 = [String]；CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1284-1288, 1309）:
    key  = args[0] 的字符串值
    dict = exm.VEvaluator.VariableData.DataDataTables
    contains = dict.ContainsKey(key)
    case Check:  return contains ? 1 : 0
```

## 备注

- 无任何既有文档可对照（两套中文文档、EM readme、EE readme 均无 `DT_*`）。
- 判定是**字典键是否命中**，不做格式校验；空字符串是合法键名（`DT_CREATE ""` 与 `DT_EXIST ""` 可成对使用）。
- 与 `MAP_EXIST`（连想数组版，见 `Emuera.EM_readme.txt:222`）语义同型，互为 Map/DT 两套容器的对应函数。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
