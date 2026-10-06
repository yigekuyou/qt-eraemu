# VARSIZE

- **类别**：命令（另有同名式中函数，见下）
- **签名**：
  - 命令：`VARSIZE <变量名>`
  - 式中函数：`int VARSIZE(str name, int dim = 0)`
- **文档来源**：`ecd/docs/translation/Command.md` → `### VARSIZE <变量名>`；式中函数见 `ecd/Expression.md` 第 287 行 `int VARSIZE(str name, int dim = 0)`。`Era-Chinese-Documentation/docs/` 未收录独立小节，仅 `zh/Variable.md` 在变量说明中提及"如果它们是 VARSIZE 指令的主题，元素的数量将分别分配给 RESULT:0 / RESULT:1 / RESULT:2"。

## 语义

命令形式：把数组变量的大小写入 `RESULT`。多维数组各维的大小从左到右依次存入 `RESULT:0`、`RESULT:1`、`RESULT:2`。CSV 变量的大小通常由 `VariableSize.csv` 决定。指令参数引用的是数组变量本身而非元素，因此即使参数里带索引、甚至索引越界（如 `VARSIZE FLAG:-1`，等同于 `VARSIZE FLAG`）也不会报错。

式中函数形式：`VARSIZE("变量名")` 返回一维长度；`VARSIZE("变量名", dim)` 返回指定维的长度。注意当 `Config.VarsizeDimConfig` 开启时第 2 参数按"第几维"（1 起点）解释，函数内部会先减 1。

## 用法

### VARSIZE <变量名>

- `<变量名>`：数组变量名，可附带索引（索引被忽略）。结果写入 `RESULT`（多维依次写 `RESULT:0..2`）。

```erb
VARSIZE FLAG
PRINTFORML <TEST1> = {RESULT:0}
VARSIZE SAVESTR
PRINTFORML <TEST2> = {RESULT:0}
VARSIZE TALENT
PRINTFORML <TEST3> = {RESULT:0}
WAIT
;结果（未修改大小的情况下）
;<TEST1> = 10000
;<TEST2> = 100
;<TEST3> = 1000
```

### int VARSIZE(str name, int dim = 0)（式中函数）

- `name`：变量名字符串。
- `dim`：维编号；省略为 0。返回该维元素数；变量不存在时抛出错误（"不是变量名"）。

```erb
PRINTFORML {VARSIZE("FLAG")}          ;10000
PRINTFORML {VARSIZE("TA", 1)}         ;TA 第 2 维大小
```

## 源码实现（emuera.em/Emuera）

- 注册（命令）：`Runtime/Script/Statements/FunctionIdentifier.cs:399`（`argb[FunctionArgType.SP_VAR], METHOD_SAFE | EXTENDED`，注释"動作が違うのでMETHOD化できない"）
- 实现（命令）：`Runtime/Script/Process.ScriptProc.cs:316`（switch-case）→ `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1691`（`VariableEvaluator.VarSize`）
- 注册（式中函数）：`Runtime/Script/Statements/Function/Creator.cs:37`（`VarsizeMethod`）
- 实现（式中函数）：`Runtime/Script/Statements/Function/Creator.Method.cs:2333`（`VarsizeMethod`）

```text
// 命令形式
函数 VarSize(varID):                          // VariableToken
    result <- RESULT 数组
    若 varID 是二维数组:
        result[0] <- varID.GetLength(0)
        result[1] <- varID.GetLength(1)
    否则若 varID 是三维数组:
        result[0..2] <- 各维 GetLength(0..2)
    否则:
        result[0] <- varID.GetLength()
    // 不检查参数里写过的索引，索引越界也无影响

// 式中函数形式
函数 VarsizeMethod.GetIntValue(exm, arguments):
    var <- IdentifierDictionary.GetVariableToken(arguments[0] 的字符串值, null, true)
    若 var == null: 抛 CodeEE（"VARSIZE 的第 1 参数不是变量名"）
    dim <- 0
    若 给出了第 2 参数: dim <- 其整数值
    若 Config.VarsizeDimConfig 且 dim > 0: dim--     // 该配置下按 1 起点的"第几维"解释
    返回 var.GetLength(dim)
```

## 备注

- 命令与式中函数是两套不同实现，行为不同：命令写入 `RESULT` 且按数组实际维数填多个结果；函数返回单一整数且第 2 参数受 `VarsizeDimConfig` 配置影响。
- 命令版对"参数带索引/越界索引不报错"有明确文档；函数版没有此特性（参数是字符串变量名）。
- 文档未提及但源码可见：函数版在变量名不是变量时抛 CodeEE；命令版 `SP_VAR` 特殊参数在解析期就确定了目标变量。
