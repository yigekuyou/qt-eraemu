# DT_RELEASE

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_RELEASE(str 数据表名)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

丢弃名为 `<数据表名>` 的数据表（连同其全部列定义与行数据），返回 `1`。

- 表不存在时**什么也不做，仍返回 1**（源码 `if (contains) dict.Remove(key); return 1;`）——与 `DT_CLEAR`/`DT_NOCASE` 表不存在返回 `-1` 的约定不同，本函数无失败返回值。
- 释放后该表名可被 `DT_CREATE` 重新建立（新建的表是全新的空表，`id` 列重新自动创建）。
- 只影响字典中该键的表对象；其它同结构表不受影响。
- 与 `DT_CLEAR` 的区别：`DT_CLEAR` 清空行但保留列结构（表仍存在），`DT_RELEASE` 整个删除（`DT_EXIST` 变 0）。

## 用法

### int DT_RELEASE(str 数据表名)
- 数据表名：字符串表达式，与 `DT_CREATE` 使用的键一致。
- 返回值：恒为 `1`（源码无失败分支）。
```erb
DT_CREATE "临时表"
DT_COLUMN_ADD "临时表", "备注", "string"
DT_RELEASE "临时表"
PRINTFORML {DT_EXIST("临时表")}        ;→ 0
PRINTFORML {DT_ROW_LENGTH("临时表")}   ;→ -1（表已不存在）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:282`（`["DT_RELEASE"] = new DataTableManagementMethod(DataTableManagementMethod.Operation.Release)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1270`（`DataTableManagementMethod`）
- 表的存储：`Runtime/Script/Statements/Variable/VariableData.cs:21`

```text
GetIntValue(exm, args)（Creator.Method.cs:1284-1288, 1310）:
    key  = args[0] 的字符串值
    dict = exm.VEvaluator.VariableData.DataDataTables
    contains = dict.ContainsKey(key)
    case Release:
        if contains: dict.Remove(key)     ; 释放表对象
        return 1                          ; 无论是否存在都返回 1
```

## 备注

- 无既有文档可对照。
- 「已存在但已释放」和「从未创建」两种情况返回值都是 1，调用方若需要区分，应先 `DT_EXIST`。
- 对应 MAP 系的 `MAP_RELEASE`（`Emuera.EM_readme.txt:226`：「第一引数で指定した連想配列を削除します。1を返す」），两者语义一致（都不检查存在性、都返回 1），可互相参照。
- 若该表在 `VarExt*.csv` 中登记为存档对象，`DT_RELEASE` 后存档时字典里已无此键，写档循环会跳过（`Runtime/Script/Statements/Variable/VariableData.cs:1017-1019`：`if (DataDataTables.ContainsKey(key))`）。
