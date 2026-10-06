# PRINTBUTTONC

- **类别**：命令
- **签名**：
  - `PRINTBUTTONC <字符串表达式>, <数值表达式或字符串表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「按钮」小节（`### PRINTBUTTON(|C|LC)`，与 `PRINTBUTTON`/`PRINTBUTTONLC` 合并记载）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

`PRINTBUTTON` 的右对齐变体：生成可以用鼠标点击的按钮，并在按钮的单元格内右对齐显示。对齐行为与 `PRINTC` 相同（右对齐，`LC` 后缀则是左对齐）。其余语义（第 2 参数指定点击时输入的数值或字符串、第 1 参数中的换行符被忽略、用于强制按钮化等）与 `PRINTBUTTON` 完全相同，数值按钮配 `INPUT`、字符串按钮配 `INPUTS` 使用。

## 用法

### `PRINTBUTTONC <字符串表达式>, <数值表达式或字符串表达式>`
- `<字符串表达式>`：按钮上显示的文本（在单元格内右对齐；换行符被忽略）。
- `<数值表达式或字符串表达式>`：点击按钮时送入的输入值。
```erb
PRINTL 是要这样么？
PRINTBUTTONC "[0] 是", 0
PRINTL
PRINTBUTTONC "[1] 否", 1
INPUT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:174`（`argb[FunctionArgType.SP_BUTTON]`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:127`（`case FunctionCode.PRINTBUTTONC`，与 128 行的 `PRINTBUTTONLC` 共用分支）；对齐文本构造与按钮节点追加 `UI/Game/EmueraConsole.Print.cs:608`（`PrintButtonC(string, string, bool)` / `PrintButtonC(string, long, bool)`）

```text
case PRINTBUTTONC / PRINTBUTTONLC:
    若 skipPrint: break
    exm.Console.UseUserStyle = true
    exm.Console.UseSetColorStyle = true
    bArg = (SpButtonArgument)func.Argument
    str = bArg.PrintStrTerm 求字符串值
    str = str.Replace("\n", "")                  # 删除换行符
    isRight = (func.FunctionCode == PRINTBUTTONC)   # C→右对齐，LC→左对齐
    若 bArg.ButtonWord 的操作数类型是 long:
        exm.Console.PrintButtonC(str, bArg.ButtonWord 求整数值, isRight)
    否则:
        exm.Console.PrintButtonC(str, bArg.ButtonWord 求字符串值, isRight)

Console.PrintButtonC(str, p, isRight):
    若 str 为空或 null: return
    printBuffer.AppendButton(CreateTypeCString(str, isRight), Style, p)
    # CreateTypeCString 把文本补空格成 PRINTC 风格的定宽单元格：
    # isRight 为真时空格补在左侧（右对齐），为假时补在右侧（左对齐）
```

## 备注

- ecd 文档把 `PRINTBUTTON`、`PRINTBUTTONC`、`PRINTBUTTONLC` 三者合并在一个小节记载，签名相同，仅说明「括号内的关键字与 `PRINTC` 相同，设置了对齐方向」；「C 右对齐 / LC 左对齐」由源码 `isRight` 标志确认（与 `PRINTC` 系的 `C`/`LC` 方向一致）。
- 与 `PRINTBUTTON` 的差异仅在对齐处理（`CreateTypeCString` 补格），按钮输入值机制完全相同。
- zh 套件未收录本命令。
