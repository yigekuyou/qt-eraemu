# PRINTBUTTONLC

- **类别**：命令
- **签名**：
  - `PRINTBUTTONLC <字符串表达式>, <数值表达式或字符串表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「按钮」小节（`### PRINTBUTTON(|C|LC)`，与 `PRINTBUTTON`/`PRINTBUTTONC` 合并记载）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

`PRINTBUTTON` 的左对齐变体：生成可以用鼠标点击的按钮，并在按钮的单元格内左对齐显示。对齐行为与 `PRINTLC` 相同。其余语义（第 2 参数指定点击时输入的数值或字符串、第 1 参数中的换行符被忽略、用于强制按钮化等）与 `PRINTBUTTON` 完全相同，数值按钮配 `INPUT`、字符串按钮配 `INPUTS` 使用。

## 用法

### `PRINTBUTTONLC <字符串表达式>, <数值表达式或字符串表达式>`
- `<字符串表达式>`：按钮上显示的文本（在单元格内左对齐；换行符被忽略）。
- `<数值表达式或字符串表达式>`：点击按钮时送入的输入值。
```erb
PRINTL 请选择：
PRINTBUTTONLC "[0] 苹果", 0
PRINTL
PRINTBUTTONLC "[1] 橘子", 1
INPUT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:175`（`argb[FunctionArgType.SP_BUTTON]`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:128`（`case FunctionCode.PRINTBUTTONLC`，与 127 行的 `PRINTBUTTONC` 共用分支）；对齐文本构造与按钮节点追加 `UI/Game/EmueraConsole.Print.cs:614`（`PrintButtonC(string, string, bool)` / `PrintButtonC(string, long, bool)`）

```text
case PRINTBUTTONC / PRINTBUTTONLC:
    若 skipPrint: break
    exm.Console.UseUserStyle = true
    exm.Console.UseSetColorStyle = true
    bArg = (SpButtonArgument)func.Argument
    str = bArg.PrintStrTerm 求字符串值
    str = str.Replace("\n", "")                      # 删除换行符
    isRight = (func.FunctionCode == PRINTBUTTONC)    # PRINTBUTTONLC 时 isRight = false（左对齐）
    若 bArg.ButtonWord 的操作数类型是 long:
        exm.Console.PrintButtonC(str, bArg.ButtonWord 求整数值, isRight)
    否则:
        exm.Console.PrintButtonC(str, bArg.ButtonWord 求字符串值, isRight)

Console.PrintButtonC(str, p, isRight):
    若 str 为空或 null: return
    printBuffer.AppendButton(CreateTypeCString(str, isRight), Style, p)
    # isRight 为 false 时：文本长度不足 PRINTC 单元格宽度则在右侧补空格（左对齐），
    # 超宽时从右侧逐个删除补入的空格
```

## 备注

- ecd 文档把三个变体合并在一个小节记载；「`LC` 为左对齐」由源码确认：`isRight` 仅在 `PRINTBUTTONC` 时为真，`PRINTBUTTONLC` 走 `CreateTypeCString` 的左对齐分支。
- 与 `PRINTBUTTON` 的差异仅在对齐处理，按钮输入值机制完全相同。
- zh 套件未收录本命令。
