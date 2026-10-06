# WHILE

- **类别**：命令（循环语法）
- **签名**：`WHILE <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md` → `### WHILE <数值表达式>`；`Era-Chinese-Documentation/docs/`（zh/Command.md）未收录。

## 语义

`WHILE`～`WEND` 是循环语法：只要 `<数值表达式>` 不为 0 就重复执行 `WHILE` 与 `WEND` 之间的内容。条件始终满足时会成为无限循环，需用 `BREAK` 退出；循环过长时 Emuera 会发出警告。用 `GOTO` 等指令直接跳入 `WHILE`～`WEND` 内部时，读到 `WEND` 后会像通常一样回到 `WHILE` 重新判定条件。与 `DO`～`LOOP` 不同，`WHILE` 先判定条件，循环体可能一次也不执行。

## 用法

### WHILE <数值表达式>

- `<数值表达式>`：循环条件；不为 0 即为真，继续循环；为 0 跳到 `WEND` 之后。

```erb
PRINTFORML %NAME:X%
;典型结构：
I = 0
WHILE I < 5
  PRINTFORL count = {I}
  I += 1
WEND
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:250`（`new WHILE_Instruction()`）；配对规则 `Runtime/Script/Statements/FunctionIdentifier.cs:465`（`funcMatch[WHILE] = "WEND"`）
- 配对与跳转设置：`Runtime/Script/Loader/ErbLoader.cs:1081-1090`（WHILE 入嵌套栈）、`:1227-1241`（WEND 处建立双向跳转：`WHILE.JumpTo = WEND 行`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3129`（`WHILE_Instruction`）

```text
// 解析期（ErbLoader）
读到 WHILE 时: 压入嵌套栈
读到 WEND 时: 弹出配对，设 WHILE.JumpTo <- WEND 行（条件为假的出口）、WEND.JumpTo <- WHILE 行

// 执行期
类 WHILE_Instruction:
    ArgBuilder <- INT_EXPRESSION
    flag <- METHOD_SAFE | EXTENDED | FLOW_CONTROL | PARTIAL
    函数 DoInstruction(exm, func, state):
        arg <- (ExpressionArgument)func.Argument
        若 arg.Term 取整值 != 0:            // 条件为真
            return                          // 顺序进入循环体（什么也不做）
        state.JumpTo(func.JumpTo)           // 条件为假：跳到 WEND 行
                                            // 主循环下一轮 ShiftNextLine，从 WEND 下一行继续

// BREAK/CONTINUE（ErbLoader.cs:1091-1111）在栈中寻找最近的
// REPEAT/FOR/WHILE/DO 作为配对目标，func.JumpTo <- 该循环行
```

## 备注

- 文档与源码一致：条件为假时跳出循环，为真时进入循环体；循环出口由 `WHILE.JumpTo`（指向 WEND 行）实现，配合主循环的 ShiftNextLine 实际从 WEND 下一行继续。
- 文档提到"循环过长时 Emuera 会发出警告"，对应主循环每 10000 行调用一次的 `checkInfiniteLoop`（`Runtime/Script/Process.ScriptProc.cs:21` 附近，受 `Config.InfiniteLoopAlertTime` 控制）。
- zh 文档未收录该命令，无从交叉核对；ecd 对 `GOTO` 跳入循环的行为说明与 WEND 的实现（总是重新判定条件）相符。
