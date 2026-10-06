# POWER

- **类别**：命令
- **签名**：
  - `POWER <变量>`, `<数值表达式>`, `<数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「算术」小节（`### POWER`）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

把幂运算结果赋值给指定变量：`POWER A, X, Y` 把 X 的 Y 次幂赋值给变量 `A`（可带数组索引、角色变量等变量目的地）。运算结果不是数值（NaN）、是无穷大、或超出 64 位符号整数范围时出错，不会写入变量。文档所述「运算结果溢出时会出错」即指后两种情况。

注意同名的式中函数 `POWER(x, y)` 返回幂值（结果代入式中），与本命令是两个不同的东西（见备注）。

## 用法

### `POWER <变量>, <数值表达式>, <数值表达式>`
- `<变量>`：接收结果的变量目的地（如 `A`、`FLAG:5`、`CFLAG:Target:10`）。
- 第 1 个 `<数值表达式>`：底数 X。
- 第 2 个 `<数值表达式>`：指数 Y。
```erb
X = 2
Y = 10
POWER A, X, Y      ;A = 1024
POWER FLAG:3, 3, 4 ;FLAG:3 = 81
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:401`（`argb[FunctionArgType.SP_POWER]`，`METHOD_SAFE | EXTENDED`，注释「引数が違うのでMETHOD化できない」）
- 实现：`Runtime/Script/Process.ScriptProc.cs:341`（`case FunctionCode.POWER` 分支，`doNormalFunction` 内）

```text
case POWER:
    powerArg = (SpPowerArgument)func.Argument    # (变量目的地, X, Y)
    x = powerArg.X 求值
    y = powerArg.Y 求值
    pow = Math.Pow(x, y)                         # 双精度浮点幂运算
    若 pow 是 NaN:
        抛出 CodeEE（"幂运算结果不是数值"）
    否则若 pow 是无穷大:
        抛出 CodeEE（"幂运算结果为无穷大"）
    否则若 pow >= long.MaxValue 或 pow <= long.MinValue:
        抛出 CodeEE（"幂运算结果(<pow>)超出 64 位符号整数范围"）
    powerArg.VariableDest.SetValue((long)pow, exm)   # 截断为整数写入变量
```

## 备注

- 同名式中函数：`POWER(int x, int y)`（`Expression.md:293`；实现 `Runtime/Script/Statements/Function/Creator.Method.cs:3086` `PowerMethod`）在表达式中求 X 的 Y 次幂并返回；检查逻辑（NaN/无穷/溢出）与本命令相同，但错误消息不同（带函数名）。两套文档中 ecd 分别在命令篇与函数篇记载，使用时注意区分。
- 结果以 `double` 计算，再截断为 64 位整数写入变量；由于先做了范围检查，不会发生溢出回绕。
- zh 套件未收录本命令。
