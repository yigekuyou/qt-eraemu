# DT_NOCASE

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_NOCASE(str 数据表名, int 忽略大小写)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

切换指定数据表的**大小写敏感**设定，成功返回 `1`；表不存在返回 `-1`。

- 赋给的是 .NET `System.Data.DataTable.CaseSensitive`，源码为 `CaseSensitive = (第2参数 == 0)`（`Runtime/Script/Statements/Function/Creator.Method.cs:1304`）：
  - 第 2 参数 **非 0**（「忽略大小写」成立）→ `CaseSensitive = false`：列名查找、`DT_SELECT` 的过滤/排序中的字符串比较**不区分大小写**；
  - 第 2 参数 **为 0**（关闭 NOCASE）→ `CaseSensitive = true`：恢复区分大小写——这也是 `DT_CREATE` 建表时的初值。
- 命名读法：函数名 DT_NO**CASE** = 「取消大小写区分」；因此「1 = 忽略大小写」是符合函数名的方向，而参数 0 表示恢复默认（区分）。
- 影响范围是 System.Data 自身的语义（列名 `Columns.Contains`/`Columns[name]`、`Select` 过滤表达式中的字符串比较与排序）：源码只改这一个属性，故「具体哪些操作随之改变」属于 .NET 行为，**推定**。
- 表结构、行数据均不变；只改比较方式。

## 用法

### int DT_NOCASE(str 数据表名, int 忽略大小写)
- 数据表名：已存在的表名。
- 忽略大小写：非 0 → 不区分大小写；0 → 区分大小写。
- 返回值：成功 `1`；表不存在 `-1`。
```erb
DT_CREATE "t"
DT_COLUMN_ADD "t", "Name", "string"
PRINTFORML {DT_NOCASE("t", 1)}   ;→ 1
PRINTFORML {DT_NOCASE("不存在", 1)}   ;→ -1
; 下面是「忽略大小写后列名查找是否也随之一并放宽」的验证；
; 其结果取决于 .NET DataColumnCollection 对 CaseSensitive 的处理（推定），
; 无论结果如何，DT_NOCASE 自身的返回值 1 / -1 是确定的
PRINTFORML {DT_COLUMN_EXIST("t", "name")}   ;（推定）忽略大小写时命中 Name 列 → 5
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:283`（`["DT_NOCASE"] = new DataTableManagementMethod(DataTableManagementMethod.Operation.Case)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1270`（`DataTableManagementMethod`，`Operation.Case` 专用参数表在 `:1276-1277`）

```text
构造（Creator.Method.cs:1276-1279）:
    if op == Case: 参数 = [String, Int]     ; 两个参数都必填
    else:          参数 = [String]
    返回类型 = long；CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1284-1308）:
    key  = args[0] 的字符串值
    dict = exm.VEvaluator.VariableData.DataDataTables
    contains = dict.ContainsKey(key)
    case Case:
        if contains:
            dict[key].CaseSensitive = (args[1].GetIntValue(exm) == 0)
            ; 参数==0 → true（区分大小写）；参数!=0 → false（忽略大小写）
            return 1
        return -1
```

## 备注

- 无既有文档可对照；参数方向由源码 `== 0` 硬编码决定，容易被反向理解，移植时务必照抄这一关系。
- `CaseSensitive` 只影响字符串比较；`id` 列是整数，`Rows.Find(id)` 不受影响。
- 与 `DT_COLUMN_OPTIONS`（命令形态的同族成员）无关：那是列选项（DEFAULT 值）命令，本函数只切换比较方式。
