# DT_COLUMN_OPTIONS

- **类别**：EE 扩展命令（Emuera 枚举成员；EM 扩展——枚举与注册均位于 `#region EM`；两套中文文档未收录，语义据源码推定）
- **签名**（推定自 `SP_DT_COLUMN_OPTIONS_ArgumentBuilder`，`Runtime/Script/Statements/ArgumentBuilder.cs:402-496`）：`DT_COLUMN_OPTIONS <数据表名>, <列名>, <选项关键字> = <值>{, <选项关键字> = <值>...}`
  - 目前仅支持一个选项关键字 `DEFAULT`（大小写不敏感），可重复书写但只有 `DEFAULT` 合法。
- **文档来源**：无任何文档收录。两套中文文档未收录；`eraTW/README集/EmueraEE Readme/`（readme 与 changelog）亦无本命令名。相关 DT 函数族（`DT_CREATE` 等）同样未见于 ecd；**语义据源码推定**。

## 语义

为**已存在**的数据表（DataTable，见 `DT_CREATE`）的指定列设置列选项。当前唯一选项：

- `DEFAULT = <值>`：设置该列的 `DefaultValue`（列默认值）。此后**新建的行**若未显式给出该列的值，则取此默认值；对已有行不追溯生效（推定：System.Data `DataColumn.DefaultValue` 语义，源码只是给该属性赋值）。
- 值的类型必须与列类型一致：字符串列 → 值须为字符串表达式；数值列（int8/int16/int32/int64）→ 值须为整数表达式；否则抛 CodeEE（`DTInvalidDataType`，`Runtime/Utils/EvilMask/Lang.cs:1189`：「{0}関数: DataTable(\"{1}\")の\"{2}\"列に違う型の値を指定しようとしています」）。
- 整数按列类型裁剪：`sbyte/short/int` 用 `Math.Min/Max` 夹到范围，`long` 原样（`Utils.DataTable.ConvertInt`，`Runtime/Utils/EvilMask/Utils.cs:323-329`）。
- 本命令无返回值；**不修改表结构**，只改列属性。
- **源码缺陷（重要）**：表名不存在时源码给 `RESULT` 写 -1、列名不存在时写 0，但**没有 return/throw 就继续执行** `dict[key]` / `dt.Columns[cName]`，随后会抛 `KeyNotFoundException`/`ArgumentException`，被 `Process.DoScript` 顶层 catch（`Runtime/Script/Process.cs:349-358`）转为脚本错误。也就是说，这两个「状态码」在现实中无法被读取到，命令要么成功、要么直接报错。

## 用法

### `DT_COLUMN_OPTIONS <数据表名>, <列名>, DEFAULT = <值>`
```erb
DT_CREATE "tbl"
DT_COLUMN_ADD "tbl", "name", "string"
DT_COLUMN_OPTIONS "tbl", "name", DEFAULT = "无名"
DT_ROW_ADD "tbl", "name", "甲"      ;显式赋值
DT_ROW_ADD "tbl"                    ;未给 name，取列默认值
PRINTFORML [{DT_CELL_GETS("tbl", 0, "name")}] [{DT_CELL_GETS("tbl", 1, "name")}]
;→ [甲] [无名]
```
### 数值列的默认值（会被裁剪到列类型范围）
```erb
DT_CREATE "tbl2"
DT_COLUMN_ADD "tbl2", "lv", "int32"
DT_COLUMN_OPTIONS "tbl2", "lv", DEFAULT = 1
DT_ROW_ADD "tbl2"
PRINTFORML {DT_CELL_GET("tbl2", 0, "lv")}    ;→ 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:389`（枚举 `DT_COLUMN_OPTIONS`，`#region EM` 自 `:388`）；`Runtime/Script/Statements/FunctionIdentifier.cs:434`（`addFunction(FunctionCode.DT_COLUMN_OPTIONS, new DT_COLUMN_OPTIONS_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2620`（`DT_COLUMN_OPTIONS_Instruction`，`#region EM_DT` 自 `:2619`）；参数构建 `FunctionArgType.SP_DT_COLUMN_OPTIONS`（`Runtime/Script/Statements/ArgumentBuilder.cs:402-496`，`argb` 登记于 `:243`）→ `Runtime/Script/Statements/Argument.cs:55`（`SpDtColumnOptions`；选项枚举 `DTOptions.Default` 于 `:57-60`）
- 相关标识符：`DT_*` 函数族注册于 `Runtime/Script/Statements/Function/Creator.cs:280-306`（`#region EM_私家版_追加関数` 自 `:217`）；表存储 `Runtime/Script/Statements/Variable/VariableData.cs:21`（`Dictionary<string, DataTable> DataDataTables`）；列默认值辅助 `Runtime/Utils/EvilMask/Utils.cs:323`

```text
参数构建 SP_DT_COLUMN_OPTIONS_ArgumentBuilder（ArgumentBuilder.cs:402）:
    解析 第1参 = dt（数据表名，字符串表达式，必填）
    解析 第2参 = column（列名，字符串表达式，必填）
    循环直到行尾:
        keyword = 当前词元.ToLower(); 跳过 keyword 与 '='
        若已到行尾 → 报「引数が足りません」错
        switch keyword:
            "default": opts.Add(Default); value = 按逗号界解析的表达式
            其他      → 报「解釈できない引数」错
    若一个选项都没有 → 报「引数が足りません」错
    返回 SpDtColumnOptions(dt, column, opts[], values[])

构造 DT_COLUMN_OPTIONS_Instruction:
    ArgBuilder = SP_DT_COLUMN_OPTIONS
    # 源码注释：//スキップ不可
    # 原 flag = IS_PRINT | IS_INPUT | EXTENDED 已注释，现行 flag = EXTENDED | METHOD_SAFE

DoInstruction(exm, func, state):
    arg  = (SpDtColumnOptions)func.Argument
    dict = exm.VEvaluator.VariableData.DataDataTables
    cName = arg.Column.GetStrValue(exm)
    key   = arg.DT.GetStrValue(exm)
    if !dict.ContainsKey(key): exm.VEvaluator.RESULT = -1      # ← 无 return（缺陷）
    dt = dict[key]                                             # 表不存在 → KeyNotFoundException
    if !dt.Columns.Contains(cName): exm.VEvaluator.RESULT = 0  # ← 无 return（缺陷）
    column = dt.Columns[cName]                                 # 列不存在 → ArgumentException
    isString = (column.DataType == typeof(string))
    for idx, opt in arg.Options:
        v = arg.Values[idx]
        switch opt:
            case Default:
                if v.GetOperandType() != (isString ? string : long):
                    throw CodeEE(DTInvalidDataType, "DT_COLUMN_OPTIONS", key, cName)
                if isString: column.DefaultValue = v.GetStrValue(exm)
                else:        column.DefaultValue = DataTable.ConvertInt(v.GetIntValue(exm), column.DataType)
```

## 备注

- 两套中文文档（ecd 与 Era-Chinese-Documentation）均未收录本命令；`DT_*` 函数族（`DT_CREATE`/`DT_COLUMN_ADD`/`DT_ROW_ADD`/`DT_CELL_GET(S)` 等，`Runtime/Script/Statements/Function/Creator.cs:280-306`）同样未见于两套文档，本命令是其唯一的**命令形态**成员（其余皆为式中函数）。语义据源码推定，`DEFAULT` 之外的选项关键字目前源码未实现、会直接报错，使用时不应假设「未来通用」。
- 枚举归属：`Runtime/Script/Statements/BuiltInFunctionCode.cs:388-390` 的 `#region EM` 只含本成员；指令类归在 `#region EM_DT`，参数构建器与 `SpDtColumnOptions` 亦标注 EM（`EM_私家版` 系），即**这是 EM 私家版扩展而非 EE 扩展**。
- 与 `COLUMNCREATE.md:40` 的结论一致（本命令与 `COLUMNCREATE` 无关），但该处把 `DT_COLUMN_OPTIONS` 描述为「下拉列框选项的枚举」并不准确：据 `Runtime/Script/Statements/BuiltInFunctionCode.cs:389` 与 `Runtime/Script/Statements/Instraction.Child.cs:2620` 的指令类核实，它实际是 DataTable 的列选项**命令**（EM 区唯一成员），与 COLUMN 系的「下拉列框」是两回事。
- 数值默认值会被裁剪而非报错（`ConvertInt` 用 `Math.Min/Max`），字符串/数值类型不符才报 `DTInvalidDataType`。
- C# 缺陷提示：表/列不存在时缺少提前返回，`RESULT=-1/0` 不可依赖。移植处理建议见 [差异记录](../../change/commands.md#dt_column_options-返回状态)。
