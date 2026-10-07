# DT_SELECT

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_SELECT(str 数据表名{, str 过滤表达式{, str 排序表达式{, ref int 输出数组}}})
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

按条件筛选行并返回命中**行数**，同时把命中行的 `id` 写入整数数组（默认写入系统整数数组 `RESULT`）。

- 过滤/排序直接交给 .NET `System.Data.DataTable.Select(filter, sort)`（`Runtime/Script/Statements/Function/Creator.Method.cs:1698-1700`），所以**语法是 DataTable 的过滤语法**，不是 EraBasic 表达式：
  - 过滤示例：`"id > 3"`、`"lv >= 10 AND lv < 20"`、`"name = '甲'"`（字符串字面量用**单引号**）；
  - 排序示例：`"id"`、`"lv DESC"`、`"name ASC, lv DESC"`；
  - 语法不合法的过滤/排序会由 .NET 抛异常（非 CodeEE，属未捕获异常，推定）。
- 四种实参形态（`OmitStart = 1`，从第 2 参起可省）：

| 写法 | 过滤 | 排序 | 输出目标 | 写入布局 |
|---|---|---|---|---|
| `DT_SELECT(表)` | 无 | 无 | `RESULT` 系统数组 | `RESULT:0` = 行数，`RESULT:1..` = 各 id |
| `DT_SELECT(表, 过滤)` | 有 | 无 | `RESULT` | 同上 |
| `DT_SELECT(表, 过滤, 排序)` | 有 | 有 | `RESULT` | 同上 |
| `DT_SELECT(表, 过滤, 排序, ref int 数组)` | 有 | 有 | 调用者数组 | `数组:0..` = 各 id（**不含**计数） |

- 返回值恒为**命中行数**（不是写入个数）：即使 `RESULT` 容量不足，多余 id 被丢弃，返回值仍为全量命中数。写入 `RESULT` 时实际写入上限是 `RESULT 数组长度 - 1`；写调用者数组时上限是数组长度。
- 表不存在 → `-1`，且不改写输出数组。
- 过滤/排序实参可以**留空**：源码用 `arguments[i] != null ? ... : null` 判空，空 → 跳过该参数（例如只想排序时可留空过滤项，具体能否书写取决于 ERB 语法的空实参写法）。
- 与 `DT_ROW_REMOVE` 的批量形态配合：`DT_SELECT` 取出的 `RESULT:1..` 就是可直接交给它删除的 id 列表。

## 用法

### int DT_SELECT(str 数据表名{, str 过滤{, str 排序}})（结果写入 RESULT）
```erb
#DIM i
DT_CREATE "t"
DT_COLUMN_ADD "t", "name", "string"
DT_COLUMN_ADD "t", "lv", "int32"
DT_ROW_ADD "t", "name", "甲", "lv", 10
DT_ROW_ADD "t", "name", "乙", "lv", 30
DT_ROW_ADD "t", "name", "丙", "lv", 20

PRINTFORML {DT_SELECT("t", "lv >= 20")}     ;→ 2
FOR i, 1, RESULT:0
	PRINTFORML 命中 id = {RESULT:i}
NEXT
PRINTFORML {DT_SELECT("t", "lv >= 20", "lv DESC")}   ;→ 2（按 lv 降序写入 RESULT:1..）
```

### int DT_SELECT(str 数据表名{, str 过滤{, str 排序}}, ref int 输出数组)
```erb
#DIM IDS, 100
PRINTFORML {DT_SELECT("t", "name = '甲'", "id", IDS)}   ;→ 1
PRINTFORML IDS:0                                        ;→ 命中行的 id
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:303`（`["DT_SELECT"] = new DataTableSelectMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1679`（`DataTableSelectMethod`）
- 系统数组：`RESULT`（整数组）为 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:2492`（`RESULT_ARRAY`）

```text
构造（Creator.Method.cs:1681-1688）:
    返回类型 = long
    argumentTypeArrayEx = [{ String, String, String, RefInt1D }, OmitStart = 1]
    ; 只有表名必填；第 2/3/4 参均可省（第 4 参必须是一维整数数组变量）
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1689-1713）:
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return -1
    dt = dict[key]
    filter = (args.Count > 1) ? (args[1] != null ? args[1] 的字符串值 : null) : null
    sort   = (args.Count > 2) ? (args[2] != null ? args[2] 的字符串值 : null) : null
    if sort != null:   res = dt.Select(filter, sort)
    elif filter != null: res = dt.Select(filter)
    else:              res = dt.Select()
    toResult = (args.Count != 4)                  ; 没给输出数组 → 写 RESULT
    output = toResult ? GlobalStatic.VEvaluator.RESULT_ARRAY
                      : (args[3] as VariableTerm).Identifier.GetArray() as long[]
    if res != null:
        count = min(res.Length, toResult ? output.Length - 1 : output.Length)
        for i = 0..count-1:
            output[toResult ? i + 1 : i] = (long)res[i][0]     ; 取命中行的第 0 列 = id
        if toResult: output[0] = res.Length                    ; RESULT:0 = 命中数
        return res.Length                                      ; 返回命中行数
    if toResult: output[0] = 0
    return 0
```

## 备注

- 无既有文档可对照。
- 取的是`res[i][0]`，即表中**第 0 列**的值；`DT_CREATE` 建的表中第 0 列就是 `id`。若用 `DT_FROMXML` 导入的表不含 `id` 或第 0 列不是 id，本函数写出的就是那一列的值（推定）。
- 返回值与写入量可能不一致（容量不足时），这是与 `ENUMFILES`/`ENUMFUNC*` 系列（返回写入量）不同的一点。
- 过滤表达式里字符串用单引号是 .NET 约定；用双引号会被解析为**列名**而非字面量（.NET 语义，推定），写错通常报解析异常。
