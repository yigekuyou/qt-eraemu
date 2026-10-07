# DT_CREATE

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_CREATE(str 数据表名)
- **文档来源**：两套中文文档（`ecd/`、`_extracted/zh/`）与 EM/EE readme 均未收录，语义据源码。同族命令 `DT_COLUMN_OPTIONS` 的既有文档（`test/data/commands/DT_COLUMN_OPTIONS.md`）已确认 `DT_*` 族注册于 `Runtime/Script/Statements/Function/Creator.cs:280-306`

## 语义

在当前游戏的「数据表仓库」中新建一张名为 `<数据表名>` 的表（.NET `System.Data.DataTable`），成功返回 `1`。

- 表名只是一个字符串键（大小写敏感，原样保存）；同一名字已存在时不新建、不覆盖，返回 `0`。
- 新建时会自动附加一个主键列 `id`：类型 `long`、`AllowDBNull = false`、`Unique = true`，并设为表的主键（`Runtime/Script/Statements/Function/Creator.Method.cs:1317-1321`）。行号（id）由 `DT_ROW_ADD` 用 `Utils.TimePoint()` 自动生成。
- 新建时把 `CaseSensitive` 置为 `true`（`Runtime/Script/Statements/Function/Creator.Method.cs:1315`），即列名/字符串比较**默认区分大小写**；可用 `DT_NOCASE` 改为不区分。
- 表属于 EM 私家版扩展的游戏状态：要随存档保存，需在 CSV 目录的 `VarExt*.csv` 里登记（`SAVE_DTS` / `GLOBAL_DTS`，`Runtime/Script/Data/ConstantData.cs:1341-1354`），并在清档/读档路径上被 `RemoveEMSaveData` 等清除（`Runtime/Script/Statements/Variable/VariableData.cs:1089-1160`）。
- 表已存在返回 0 且**不改动**已有表；销毁用 `DT_RELEASE`，清空行用 `DT_CLEAR`。

## 用法

### int DT_CREATE(str 数据表名)
- 数据表名：字符串表达式，任意键名。
- 返回值：新建成功 `1`；已存在 `0`。
```erb
DT_CREATE "玩家表"          ;→ 1
DT_CREATE "玩家表"          ;→ 0（已存在）
PRINTFORML {DT_EXIST("玩家表")}   ;→ 1
PRINTFORML {DT_COLUMN_LENGTH("玩家表")}   ;→ 1（只有自动的 id 列）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:280`（`["DT_CREATE"] = new DataTableManagementMethod(DataTableManagementMethod.Operation.Create)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1270`（`DataTableManagementMethod`，`Operation` 枚举于 `:1272`）
- 表的存储：`Runtime/Script/Statements/Variable/VariableData.cs:21`（`Dictionary<string, DataTable> DataDataTables`，`System.Data.DataTable`）

```text
构造 DataTableManagementMethod(Create)（Creator.Method.cs:1273-1282）:
    返回类型 = long
    参数 = [String]（argumentTypeArray，不含省略）
    CanRestructure = false
    （同族 Create/Check/Release/Clear/Case 共用本类；Case 用 [String, Int]）

GetIntValue(exm, args)（Creator.Method.cs:1284-1322）:
    key  = args[0] 的字符串值
    dict = exm.VEvaluator.VariableData.DataDataTables      ; VariableData.cs:21
    contains = dict.ContainsKey(key)
    switch (op):
        Clear:
            if contains: dict[key].Clear(); return 1         ; 清空行，保留列
            return -1
        Case:                                                ; DT_NOCASE
            if contains: dict[key].CaseSensitive = (args[1] != 0)
                         return 1
            return -1
        Check:  return contains ? 1 : 0                       ; DT_EXIST
        Release:
            if contains: dict.Remove(key)
            return 1                                          ; 不存在也返回 1
    ; 以下只剩 Create
    if contains: return 0                                    ; 已存在 → 不覆盖
    dt = new DataTable(key) { CaseSensitive = true }
    c  = dt.Columns.Add("id", typeof(long))
    c.AllowDBNull = false
    c.Unique = true
    dict[key] = dt
    dt.PrimaryKey = [c]                                      ; 以 id 为主键
    return 1
```

## 备注

- 两套中文文档与 EM readme 均无 `DT_*` 条目；本函数族是 EM 私家版（`#region EM_私家版_追加関数` 自 `Runtime/Script/Statements/Function/Creator.cs:217`）新增功能，语义完全由源码得出。
- 与同名的既存概念无关：EraBasic 里没有 DataTable 变量类型，表只能通过这套函数访问（另有命令形态的 `DT_COLUMN_OPTIONS`）。
- `id` 列不能删除（`DT_COLUMN_REMOVE` 拒绝）也不能改（`DT_CELL_SET`/`DT_ROW_SET` 拒绝），只能随行自动生成。
- 表体不会自动进入存档：EM 私家版用 `VarExt*.csv` 的 `SAVE_DTS` / `GLOBAL_DTS` 行登记要保存的表名（`Runtime/Script/Data/ConstantData.cs:1341-1354`），未登记的表读档后不存在——此点源码可证，属**实现**必须注意的行为。
- 作为**语句**调用（如示例）也合法：所有式中函数都被登记为 `METHOD_SAFE | EXTENDED` 的指令，语句形态下返回值写入 `RESULT`，见 `Runtime/Script/Statements/Instraction.Child.cs:579-598`（`METHOD_Instruction`）、`Runtime/Script/Statements/FunctionIdentifier.cs:458`（所有式中函数注册为 `methodInstruction`，其 flag 于 `Runtime/Script/Statements/Instraction.Child.cs:584` 为 `METHOD_SAFE | EXTENDED`）。DT 族全套都可这样当语句写。
