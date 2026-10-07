# DT_COLUMN_ADD

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_COLUMN_ADD(str 数据表名, str 列名{, str|int 类型{, int 允许空值}})
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码。列「类型关键字 ↔ 编号」的对应由 `Runtime/Utils/EvilMask/Utils.cs:282-314` 的 `builtInDTTypes` 表确定

## 语义

在已存在的数据表末尾追加一列，成功返回 `1`；表不存在返回 `-1`；该列名已存在返回 `0`（不报错、不覆盖）。

- 类型可用**字符串关键字**或**编号**两种写法（两者由实参的表达式类型区分，不是靠内容判断）：
  - 字符串：`"int8"`、`"int16"`、`"int32"`、`"int64"`、`"string"`；
  - 整数编号：`1` = int8(sbyte)、`2` = int16(short)、`3` = int32(int)、`4` = int64(long)、`5` = string。
- 类型省略时调用 `dt.Columns.Add(cName)` 而不指定类型，列类型取 .NET `DataColumn` 的默认值 **string**（推定：源码只是不传类型；`DT_COLUMN_EXIST` 反读时应得 5）。
- 第 4 参数是「允许空值」：省略或非 0 → `AllowDBNull = true`（默认允许）；显式给 0 → `AllowDBNull = false`（推定：源码为 `nullable = args.Count == 4 ? args[3] != 0 : true`，实际只赋给 `DataColumn.AllowDBNull`）。
- 类型关键字不认识、或编号不在 1..5 时抛 `CodeEE`（`trerror.UnsupportedType`，`Runtime/Script/Statements/Function/Creator.Method.cs:1382`），不是返回错误码。
- 列名 `"id"` 已由 `DT_CREATE` 自动创建，再添加同名（同大小写）列会走「已存在 → 0」分支；表区分大小写时添加 `"ID"` 是另一列（可加、也可被 `DT_COLUMN_REMOVE` 删除）。
- 不影响已有行：列被追加后，已有行在该列上是 NULL（若 `AllowDBNull = false`，`DT_ROW_SET` 想把它改为 NULL 之类操作会由 .NET 拒绝，属推定）。

## 用法

### int DT_COLUMN_ADD(str 数据表名, str 列名, str 类型) / (…, int 类型编号)
- 数据表名：已存在的表。
- 列名：新列名（不能与现有列同名，否则返回 0）。
- 类型：`"int8"|"int16"|"int32"|"int64"|"string"` 或 `1|2|3|4|5`。
- 返回值：成功 `1`；已存在 `0`；表不存在 `-1`；类型不可识别 → CodeEE。
```erb
DT_CREATE "t"
DT_COLUMN_ADD "t", "name", "string"     ;→ 1
DT_COLUMN_ADD "t", "lv", "int32"        ;→ 1
DT_COLUMN_ADD "t", "memo", 5            ;→ 1（5 == string）
DT_COLUMN_ADD "t", "name", "string"     ;→ 0（列名重复）
PRINTFORML {DT_COLUMN_EXIST("t", "lv")}      ;→ 3（int32）
PRINTFORML {DT_COLUMN_LENGTH("t")}           ;→ 4（id + 3 列）
```

### int DT_COLUMN_ADD(str 数据表名, str 列名{, int 允许空值})（省略类型）
```erb
DT_CREATE "t2"
DT_COLUMN_ADD "t2", "only"              ;→ 1（类型省略 → 默认 string 列）
PRINTFORML {DT_COLUMN_EXIST("t2", "only")}   ;→ 5（推定：.NET 列默认类型为 string）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:287`（`["DT_COLUMN_ADD"] = new DataTableColumnManagementMethod(DataTableColumnManagementMethod.Operation.Create)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1325`（`DataTableColumnManagementMethod`）
- 类型表：`Runtime/Utils/EvilMask/Utils.cs:282`（`builtInDTTypes = [sbyte, short, int, long, string]`）、`:299`（`TypeToInt`）、`:308`（`IntToType`）、`:313`（`NameToType`）

```text
构造（Creator.Method.cs:1331-1334）:
    op == Create → argumentTypeArrayEx = [{ String, String, Any, Int }, OmitStart = 2]
    返回类型 = long；CanRestructure = false
    ; OmitStart=2：第 1、2 参必填，第 3 参（类型）与第 4 参（可空）可省

GetIntValue(exm, args)（Creator.Method.cs:1345-1391）:
    key = args[0] 的字符串值
    dict = exm.VEvaluator.VariableData.DataDataTables
    if !dict.ContainsKey(key): return -1                       ; 表不存在
    dt = dict[key]
    cName = args[1] 的字符串值
    if dt.Columns.Contains(cName): return 0                    ; 列已存在
    t = null
    if args.Count >= 3:                                        ; 指定了类型
        if args[2].GetOperandType() == typeof(string):
            t = Utils.DataTable.NameToType(args[2].GetStrValue(exm))   ; "int8".."string"
        else:
            t = Utils.DataTable.IntToType(args[2].GetIntValue(exm))    ; 1..5
        if t == null: throw CodeEE(UnsupportedType, Name)       ; 不认识的类型 → 报错
    nullable = (args.Count == 4) ? (args[3].GetIntValue(exm) != 0) : true
    if t != null: dc = dt.Columns.Add(cName, t)
    else:         dc = dt.Columns.Add(cName)                    ; 类型省略 → .NET 默认 string
    dc.AllowDBNull = nullable
    return 1
```

## 备注

- 无既有文档可对照。类型编号表与 `DT_COLUMN_EXIST` 的返回值共用同一套编码（1..5），互为逆操作。
- 「类型实参是字符串还是整数」由**表达式的静态类型**决定：`DT_COLUMN_ADD "t","c", 3` 走编号分支，`DT_COLUMN_ADD "t","c", "3"` 走名称分支且 `"3"` 不是合法名 → 报 `UnsupportedType`。
- 名称分支的大小写敏感：`NameToType` 用精确匹配查 `"int8"/"int16"/"int32"/"int64"/"string"`（`Runtime/Utils/EvilMask/Utils.cs:291-297, 313-317`），写成 `"STRING"`、`"Int32"` 会**报错**而不是被接受（与 EraBasic 标识符本身忽略大小写的习惯不同，容易踩坑）。
- 列顺序即添加顺序，`DT_CREATE` 建的 `id` 永远是第 0 列（`DT_COLUMN_NAMES` 可读出）。
- 源码没有提供「修改列类型」的功能：改类型只能删列重加（`DT_COLUMN_REMOVE` 不允许删 `id`）。
