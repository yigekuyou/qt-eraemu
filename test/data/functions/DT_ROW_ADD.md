# DT_ROW_ADD

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_ROW_ADD(str 数据表名{, str 列名, 值 ...})
- **签名**：int DT_ROW_ADD(str 数据表名, ref str 列名数组, ref 值数组, int 个数)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义由源码得出

## 语义

向指定表**追加一行**，返回该行的 `id`（主键值）；表不存在返回 `-1`。

- 新行的 `id` 由 `Utils.TimePoint()` 生成（`Runtime/Script/Statements/Function/Creator.Method.cs:1470`）：它是 `DateTime.Now.Ticks` 基准 + 秒表计数的**高精度时间点数值**（约 6.3e17 量级，随运行环境变化），用于保证唯一与非空，**不可预测、不可硬编码**（`Runtime/Utils/EvilMask/Utils.cs:53, 206`）。
- 其余列的值由「列名, 值」成对给出，可一次给多对；未给出的列保持 NULL（列有 `DEFAULT` 选项时取默认值——见 `DT_COLUMN_OPTIONS` 命令）。
- 值类型必须与列类型一致：字符串列 → 字符串表达式；数值列（int8/16/32/64）→ 整数表达式；不一致抛 `CodeEE`（`DTInvalidDataType`）。列名指定为 `id`、或列名不存在，同样抛 `CodeEE`（`DTCanNotEditIdColumn` / `DTLackOfNamedColumn`）。
- 「值」位置留空（void 实参）会写入 `DBNull`（源码 `SetValue` 中 `v == null → row[name] = DBNull.Value`；可用 `DT_CELL_ISNULL` 验证）。
- **两种形态**（实参个数决定走哪条分支）：
  1. 成对形态：`DT_ROW_ADD(表名{, 列名, 值 ...})` —— 0 对也合法（只建含 `id` 的行）；但**必须成对**，个数为 odd 个实参（不含表名）在参数检查阶段就报错（`ArgsNotFitExpr`）；
  2. 数组形态：`DT_ROW_ADD(表名, 列名数组, 值数组, 个数)` —— 恰好 4 个实参时启用，按 `min(列名数组长度, 值数组长度, 个数)` 逐项赋值。

## 用法

### int DT_ROW_ADD(str 数据表名{, str 列名, 值 ...})
- 数据表名：已存在的表。
- 列名 / 值：可重复的成对实参；列不能是 `id`。
- 返回值：新行 `id`（大整数）；表不存在 `-1`。
```erb
#DIM newid
DT_CREATE "t"
DT_COLUMN_ADD "t", "name", "string"
DT_COLUMN_ADD "t", "lv", "int32"
DT_ROW_ADD "t"                                ; 只建带 id 的行
newid = DT_ROW_ADD "t", "name", "甲", "lv", 10
PRINTFORML {newid}                            ; 新行 id（时间点数值）
PRINTFORML {DT_CELL_GETS("t", 1, "name")}     ;→ 甲
DT_ROW_ADD "t", "name", , "lv", 3             ; name 列写 NULL
PRINTFORML {DT_CELL_ISNULL("t", 2, "name")}   ;→ 1
```

### int DT_ROW_ADD(str 数据表名, ref str 列名数组, ref 值数组, int 个数)
```erb
; 批量写字符串列：值数组必须是字符串数组
#DIMS CN = 2, "name", "memo"
#DIMS VC = 2, "甲", "第一条"
DT_ROW_ADD "t", CN, VC, 2        ;→ 新行 id（name ← 甲, memo ← 第一条）

; 批量写数值列：值数组必须是整数数组
#DIMS CN2 = 2, "lv", "lv2"
#DIM VN, 2
VN:0 = 11
VN:1 = 22
DT_ROW_ADD "t", CN2, VN, 2       ;→ 新行 id
PRINTFORML {DT_CELL_GET("t", DT_ROW_LENGTH("t") - 1, "lv2")}   ;→ 22
```
- `个数` 是「最多赋值几项」，实际项数 = `min(列名数组长度, 值数组长度, 个数)`。
- **不能混用**：一次调用的值数组要么全按字符串处理、要么全按整数处理（由值数组的静态类型决定），因此数组形态无法一次写入「一个字符串列 + 一个数值列」。

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:293`（`["DT_ROW_ADD"] = new DataTableRowSetMethod(DataTableRowSetMethod.Operation.Add)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1393`（`DataTableRowSetMethod`；`Operation` 于 `:1395`，`CheckName`/`SetValue` 于 `:1413-1450`）
- id 生成：`Runtime/Utils/EvilMask/Utils.cs:206`（`TimePoint()`），基准 `:53`（`stopwatch_base = DateTime.Now.Ticks`）

```text
构造（Creator.Method.cs:1399-1403）:
    op == Add → argumentTypeArrayEx = [
        { String, VariadicString, VariadicAny }, MatchVariadicGroup = true, OmitStart = 1
        { String, RefString1D, RefAny1D, Int }
    ]
    返回类型 = long；CanRestructure = false
    ; 规则1：表名后跟可变组（列名:str, 值:any），MatchVariadicGroup 要求成对 → 实参个数（除表名）必须为偶数
    ; 规则2：恰好 4 参的数组形态

GetIntValue(exm, args)（Creator.Method.cs:1451-1510）:
    b = 0（Add）／1（Set）
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return -1
    dt = dict[key]
    if op == Set:
        row = dt.Rows.Find(args[1])，找不到 → return -2
    else:
        row = dt.NewRow()
        row[0] = Utils.TimePoint()             ; id 列 = 时间点
    if args.Count == b + 4:                    ; 数组形态
        names = (args[b+1] as VariableTerm).Identifier.GetArray() as string[]
        count = min(names.Length, args[b+3])
        if args[b+2].GetOperandType() == typeof(string):
            vals = args[b+2] 的字符串数组；count = min(vals.Length, count)
            for i = 0..count-1: SetValue(row, dt, names[i], key, vals[i])   ; 字符串列
        else:
            vals = 整数数组；count = min(vals.Length, count)
            for i = 0..count-1: SetValue(row, dt, names[i], key, vals[i])   ; 数值列
    else:
        pos = b + 1
        while pos < args.Count:
            SetValue(row, dt, args[pos] 的字符串值, key, exm, args[pos+1])
            pos += 2
    if op == Add:
        dt.Rows.Add(row)                       ; 主键冲突 → .NET 异常（未捕获）
        return (long)row[0]                    ; 返回新行 id
    return 已赋值的列数

SetValue(row, dt, name, key, exm, v)（Creator.Method.cs:1413-1436）:
    if name == "id": throw CodeEE(DTCanNotEditIdColumn)
    if !dt.Columns.Contains(name): throw CodeEE(DTLackOfNamedColumn)
    if v == null: row[name] = DBNull.Value; return
    isString = (列类型 == typeof(string))
    if v.GetOperandType() != (isString ? string : long): throw CodeEE(DTInvalidDataType)
    字符串列 → row[name] = v.GetStrValue(exm)
    数值列   → row[name] = Utils.DataTable.ConvertInt(v.GetIntValue(exm), 列类型)
                    ; ConvertInt（Utils.cs:323）：sbyte/short/int 夹到范围，long 原样
```

## 备注

- 无既有文档可对照。
- 成对形态里「值」写 `Any`（`VariadicAny`）而非限定类型，类型校验推迟到运行期 `SetValue`（不一致即抛 CodeEE）；数组形态则按值数组的静态类型整批处理，**不能混用**：一批里既有字符串列又有数值列时，数组形态会因类型不符抛 `DTInvalidDataType`（源码 `SetValue(row,dt,name,key,long)` 要求列不是字符串列）。
- 返回值是新行 `id`（不是 1/0）。id 值来自计时器，同一脚本多次运行不同，**不要**写 `IF DT_ROW_ADD(...) == 1` 这类判断。
- 主键唯一性由 .NET 保证；理论上同一 tick 连加两行会撞主键抛 `ConstraintException`（非 CodeEE）——概率极低，但属于潜在缺陷（推定）。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
