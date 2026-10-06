# REDRAW

- **类别**：命令
- **签名**：`REDRAW <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「REDRAW `<数值表达式>`」节；`zh/Command.md` 未收录。

## 语义

控制画面绘制的指令。参数值含义（文档）：

- 0 —— 暂停画面自动绘制，只在需要用户输入时更新画面。
- 1 —— 进行画面自动绘制，画面更新的频率为设置中的每秒帧数。
- 2 —— 与 0 相同，并在执行 REDRAW 时强制更新画面一次。
- 3 —— 与 1 相同，并在执行 REDRAW 时强制更新画面一次。

通常用来绘制不会「晃动」的界面。用 `CURRENTREDRAW` 指令可获取当前绘制方式（返回 0 或 1，RESULT:0；默认 1，暂停时 0）。

## 用法

### `REDRAW <模式值>`
- `<模式值>`：数值表达式，0～3（语义见上；实现上按位判定）。

```erb
REDRAW 0        ; 暂停自动绘制
…… 一连串 PRINT，画面不闪烁 ……
REDRAW 1        ; 恢复自动绘制
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:298`（`argb[FunctionArgType.INT_EXPRESSION], METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:520-526`（switch-case `FunctionCode.REDRAW`）；`SetRedraw` 在 `UI/Game/EmueraConsole.cs:1540-1547`

```text
若参数是常量：iValue = 常量值；否则 iValue = 参数表达式求值（整数）。
控制台.SetRedraw(iValue):
    若 (iValue & 1) == 0（0 或 2）：redraw = ConsoleRedraw.None（暂停自动绘制）
    否则（1 或 3）：redraw = ConsoleRedraw.Normal（自动绘制）
    若 (iValue & 2) != 0（2 或 3）：RefreshStrings(true)（立即强制刷新画面一次）
```

## 备注

- 文档按 0～3 四档描述，源码实现为按位运算：bit0 决定是否自动绘制、bit1 决定是否强制刷新一次。对 0～3 两者语义一致；但源码对超出 0～3 的值（如 4、5）也会按位处理，文档未提及。
- 文档说「CURRENTREDRAW 默认返回 1」；源码 `ConsoleRedraw redraw = ConsoleRedraw.Normal` 初始值与之相符。
- `zh/Command.md` 未收录。
