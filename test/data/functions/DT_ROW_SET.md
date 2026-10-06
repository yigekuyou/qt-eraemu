# DT_ROW_SET

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_ROW_SET(str 数据表名, int id{, str 列名, 值 ...})
- **签名**：int DT_ROW_SET(str 数据表名, int id, ref str 列名数组, ref 值数组, int 个数)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义由源码得出

## 语义

按主键 `id` 定位已存在的行，改写其若干列的值，返回**被赋值的列数**。

- 表不存在 → `-1`；表存在但没有该 `id` 的行 → `-2`。
- 与 `DT_ROW_ADD` 共用实现类与同一套 `SetValue` 校验：列名为 `id` 或缺列报 `CodeEE`（`DTCanNotEditIdColumn` / `DTLackOfNamedColumn`）；值类型与列类型不符报 `CodeEE`（`DTInvalidDataType`）。
- 值留空（void 实参）→ 写 `DBNull`。
- 实参个数规则（源码 `argumentTypeArrayEx`，**两种形态都不许省略列/值对**）：
  1. 成对形态至少一组「列名, 值」，即 `DT_ROW_SET(表, id)` **不合法**（参数检查报 `NotEnoughArgs`），最少 4 个实参；
  2. 数组形态恰好 5 个实参：`(表名, id, 列名数组, 值数组, 个数)`。
- 只能按 `id` 定位，不能按行序号改写（序号定位请用 `DT_CELL_SET` 的第 4 参数形式）。
- 数值列写入时会按列类型裁剪（`Utils.DataTable.ConvertInt`：int8/int16/int32 夹到边界值，int64 原样）。

## 用法

### int DT_ROW_SET(str 数据表名, int id, str 列名, 值{, str 列名, 值 ...})
- 数据表名：已存在的表。
- id：由 `DT_ROW_ADD` 返回、或行 0 列（`id` 列）读到的整数值。
- 列名 / 值：至少一组；列不能是 `id`。
- 返回值：成功赋值的列数；表不存在 `-1`；无此行 `-2`。
```erb
#DIM rid
DT_CREATE "t"
DT_COLUMN_ADD "t", "name", "string"
DT_COLUMN_ADD "t", "lv", "int32"
rid = DT_ROW_ADD "t", "name", "甲", "lv", 10
PRINTFORML {DT_ROW_SET("t", rid, "name", "乙", "lv", 99)}   ;→ 2
PRINTFORML {DT_CELL_GETS("t", 0, "name")}                   ;→ 乙
PRINTFORML {DT_ROW_SET("t", rid + 1, "lv", 1)}              ;→ -2（无此行）
PRINTFORML {DT_ROW_SET("no_such", rid, "lv", 1)}            ;→ -1
```

### int DT_ROW_SET(str 数据表名, int id, ref str 列名数组, ref 值数组, int 个数)
```erb
; 批量写数值列：列名数组里全是数值列，值数组是整数数组
#DIMS CN = 2, "lv", "lv2"
#DIM VN, 2
VN:0 = 77
VN:1 = 88
PRINTFORML {DT_ROW_SET("t", rid, CN, VN, 2)}   ;→ 2（lv 与 lv2 同时改写）

; 批量写字符串列：值数组换成字符串数组
#DIMS CN2 = 1, "name"
#DIMS VC = 1, "丙"
PRINTFORML {DT_ROW_SET("t", rid, CN2, VC, 1)}  ;→ 1（name ← 丙）
; 若把 VC 换成整数数组去写 "name"（字符串列），会在运行期抛 CodeEE（DTInvalidDataType）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:294`（`["DT_ROW_SET"] = new DataTableRowSetMethod(DataTableRowSetMethod.Operation.Set)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1393`（`DataTableRowSetMethod`）

```text
构造（Creator.Method.cs:1404-1408）:
    op == Set → argumentTypeArrayEx = [
        { String, Int, VariadicString, VariadicAny }, MatchVariadicGroup = true
        { String, Int, RefString1D, RefAny1D, Int }
    ]
    返回类型 = long；CanRestructure = false
    ; 规则1 无 OmitStart → 至少 4 参（含 1 组列名/值）；规则2 恰好 5 参

GetIntValue(exm, args)（Creator.Method.cs:1451-1510）:
    b = 1（Set）
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return -1
    dt = dict[key]
    idx = args[1].GetIntValue(exm)
    row = dt.Rows.Find(idx)        ; 按主键查找
    if row == null: return -2
    if args.Count == 5:            ; 数组形态
        names = args[2] 的字符串数组
        count = min(names.Length, args[4])
        if args[3].GetOperandType() == typeof(string): 按字符串逐项 SetValue
        else:                                          按整数逐项 SetValue
        return 赋值成功的列数（赋值失败的项会抛 CodeEE，不会计数）
    pos = 2
    while pos < args.Count:        ; 成对形态
        name = args[pos] 的字符串值
        SetValue(row, dt, name, key, exm, args[pos+1])
        pos += 2
        cCount++
    return cCount

SetValue（Creator.Method.cs:1413-1450）:
    校验列名（拒绝 id、缺列），值类型须与列类型一致；v == null → 写 DBNull
    数值列按 ConvertInt（Utils.cs:323）裁剪后写入
```

## 备注

- 无既有文档可对照。
- 返回值是**列数**而非 1/0，且只在全部列赋值成功时返回；中途遇到非法列或类型不符会直接抛 CodeEE（不是返回错误码），此时已完成的赋值不会回滚。
- `DT_ROW_ADD` 与 `DT_ROW_SET` 的差异：Add 返回新行 id 且最多 3 参以下的写法合法（无列对也可），Set 返回列数且必须带至少一组列/值。
- 数组形态的值数组与列类型必须整体匹配，故不能在一次调用里混写字符串列与数值列。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
