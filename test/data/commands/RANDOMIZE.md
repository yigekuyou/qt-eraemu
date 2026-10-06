# RANDOMIZE

- **类别**：命令
- **签名**：`RANDOMIZE <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「随机数的控制」节的「RANDOMIZE `<数值表达式>`」；`zh/Command.md` 未收录。

## 语义

用指定值初始化随机数发生器（种子）。用相同的值初始化时，之后 `RAND` 必定返回相同的结果序列。

相关的配套命令：`DUMPRAND` 把当前随机数状态保存到 `RANDDATA` 变量，`INITRAND` 从 `RANDDATA` 读取恢复状态；由于 `RANDDATA` 是会保存的变量，保存前 `DUMPRAND`、读档后 `INITRAND` 即可延续相同的随机数序列（ecd/Command.md 随机数控制节）。

## 用法

### `RANDOMIZE <种子值>`
- `<种子值>`：数值表达式，作为随机数发生器的种子。

```erb
RANDOMIZE GETTIME()   ; 或 RANDOMIZE 12345 —— 相同种子将得到相同的 RAND 序列
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:294`（`new RANDOMIZE_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1762-1785`（类 `RANDOMIZE_Instruction`，flag = `METHOD_SAFE | EXTENDED`，参数 `INT_EXPRESSION_NULLABLE`）；`Randomize` 在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:41-44`

```text
若参数是常量：iValue = 常量值；否则 iValue = 参数表达式求值（整数）。
若 JSONConfig.Data.UseNewRandom（新版随机数模式已启用）：
    ParserMediator.Warn（「RANDOMIZE 被忽略」警告），并刷新警告列表。
    ——即什么都不做。
否则：
    vEvaluator.Randomize(iValue):
        rand = new Random(种子 iValue)    ; 用种子重建随机数发生器
```

## 备注

- 文档（ecd/Command.md）只描述了经典行为「用指定值初始化随机数，相同值必得相同 RAND 序列」。
- 源码差异：当配置启用 `UseNewRandom`（JSON 配置的新随机数模式）时，`RANDOMIZE` 不再起作用，只发出「被忽略」的警告——这是文档未反映的本仓库行为。
- 注册处的参数类型为 `INT_EXPRESSION_NULLABLE`，即参数在语法上可省略（省略时相当于 0），文档签名仍写作必须给出 `<数值表达式>`。
- `zh/Command.md` 未收录。
