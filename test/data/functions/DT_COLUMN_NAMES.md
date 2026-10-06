# DT_COLUMN_NAMES

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_COLUMN_NAMES(str 数据表名{, ref str 输出数组})
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

取出指定数据表的**列名一览**，按列顺序（第 0 个永远是自动主键 `id`）写入目标字符串数组，返回列数。表不存在返回 `-1`。

- 目标数组的指定方式：
  - 省略第 2 参数 → 写入系统字符串数组 `RESULTS`（`RESULTS:0` 起依次是列名）；
  - 给出第 2 参数 → 必须是一维字符串数组变量，直接写入其中（不写 `RESULTS`）。
- 返回值是**列数**（`dt.Columns.Count`），不是「写入的元素个数」。
- **源码缺陷**：写入循环 `for (i = 0; i < dt.Columns.Count; i++) output[i] = ...` 没有做目标数组长度检查（`Runtime/Script/Statements/Function/Creator.Method.cs:1356`）。若传入的数组比列数短，会抛 `IndexOutOfRangeException`（非 CodeEE，属未捕获异常），因此第 2 参数数组必须至少与列数等长。返回值也不受数组长度影响。

## 用法

### int DT_COLUMN_NAMES(str 数据表名)（写入 RESULTS）
```erb
#DIM i
DT_CREATE "t"
DT_COLUMN_ADD "t", "name", "string"
DT_COLUMN_ADD "t", "lv", "int32"
PRINTFORML {DT_COLUMN_NAMES("t")}      ;→ 3（列数）
PRINTFORML [{RESULTS:0}] [{RESULTS:1}] [{RESULTS:2}]   ;→ [id] [name] [lv]
```

### int DT_COLUMN_NAMES(str 数据表名, ref str 列名数组)
```erb
#DIMS COLNAMES, 10      ; 容量必须 ≥ 列数
PRINTFORML {DT_COLUMN_NAMES("t", COLNAMES)}   ;→ 3
PRINTFORML [{COLNAMES:0}] [{COLNAMES:2}]      ;→ [id] [lv]
PRINTFORML {DT_COLUMN_NAMES("no_such")}       ;→ -1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:288`（`["DT_COLUMN_NAMES"] = new DataTableColumnManagementMethod(DataTableColumnManagementMethod.Operation.Names)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1325`（`DataTableColumnManagementMethod`）

```text
构造（Creator.Method.cs:1335-1338）:
    op == Names → argumentTypeArrayEx = [{ String, RefString1D }, OmitStart = 1]
    返回类型 = long；CanRestructure = false
    ; 第 2 参必须是一维字符串数组变量（RefString1D），可省略

GetIntValue(exm, args)（Creator.Method.cs:1345-1358）:
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return -1
    dt = dict[key]
    if op == Names:
        if args.Count > 1 && args[1] is VariableTerm v:
            output = v.Identifier.GetArray() as string[]        ; 调用者提供的一维数组
        else:
            output = exm.VEvaluator.RESULTS_ARRAY               ; RESULTS 系统数组（VariableEvaluator.cs:2517）
        for i = 0 .. dt.Columns.Count-1:
            output[i] = dt.Columns[i].ColumnName                ; ← 无越界保护（数组短则抛异常）
        return dt.Columns.Count
```

## 备注

- 无既有文档可对照。
- 与 `MAP_GETKEYS`（`Emuera.EM_readme.txt:256`）风格一致（都可写入指定字符串数组），但 `MAP_GETKEYS` 还有「往RESULTS写」与「返回逗号串」等 3 种形态；DT 版只有两种形态且返回值含义不同（本函数返回列数而非写入数）。
- 第 2 参数省略时写 `RESULTS`，注意不要与 `RESULT`（整数数组）混淆；`RESULTS` 长度由系统决定，列数超过其容量时同样会越界抛异常。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
