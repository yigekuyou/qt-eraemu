# REUSELASTLINE

- **类别**：命令
- **签名**：REUSELASTLINE `<FORM格式文本>`
- **文档来源**：`ecd/docs/translation/Command.md`（REUSELASTLINE 小节）；Era-Chinese-Documentation 未收录该命令

## 语义

将带 FORM 格式的文本输出到屏幕的最后一行。当紧接着用户输入了内容时，将刚才输出的一行替换为用户当前输入的内容。通常用在 `INPUT`、`INPUTS` 的循环处理中，用于处理用户的无效输入。参数格式与 `PRINTFORML` 一样。

## 用法

### REUSELASTLINE `<FORM格式文本>`
- `<FORM格式文本>`：与 `PRINTFORML` 相同的格式字符串（支持 `{表达式}`、`%字符串表达式%`，末尾换行）。

```erb
$INPUT_LOOP
PRINTL 请输入一个数值：
INPUT
IF RESULT == 0
	REUSELASTLINE 输入无效，请重新输入。
	GOTO INPUT_LOOP
ENDIF
PRINTFORMW 你输入了 {RESULT}。
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:206` → `new REUSELASTLINE_Instruction()`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:40`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:661`（`REUSELASTLINE_Instruction`）；核心逻辑在 `UI/Game/EmueraConsole.Print.cs:334`（`EmueraConsole.PrintTemporaryLine`）

```text
类 REUSELASTLINE_Instruction:
  构造: 参数构造器 = FORM_STR_NULLABLE（FORM 格式字符串，可为空）;
        标志 = METHOD_SAFE | EXTENDED | IS_PRINT

  DoInstruction(exm, func, state):
    term = ((ExpressionArgument)func.Argument).Term
    str = term.GetStrValue(exm)              // 展开 FORM 格式
    exm.Console.PrintTemporaryLine(str)      // 见下

EmueraConsole.PrintTemporaryLine(str):
    PrintSingleLine(str, true)
    // 第二参数为 true：以“临时行”方式打印——本次输出占据屏幕最后一行，
    // 当用户随后输入内容时，该行被替换为用户输入的内容；
    // 且下次向屏幕追加输出时复用该行位置（最終行を書き換え＋再利用）。
```

## 备注

- 文档中「替换为用户输入内容」「复用最后一行」的行为由控制台 `PrintSingleLine(str, true)` 的临时行机制实现，指令层只是转调。
- 与 `CLEARLINE` 不同：REUSELASTLINE 不删除已有行，而是先把光标行改写为给定文本，等待下一次输入时被替换。
- Era-Chinese-Documentation 未收录本命令。
