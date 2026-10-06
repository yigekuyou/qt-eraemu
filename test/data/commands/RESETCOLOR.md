# RESETCOLOR

- **类别**：命令
- **签名**：RESETCOLOR
- **文档来源**：`ecd/docs/translation/Command.md`（SETCOLOR/RESETBGCOLOR 一节）；Era-Chinese-Documentation 未单独收录该命令

## 语义

将文字颜色恢复为默认颜色（配置项 `Config.ForeColor`）。颜色一经 `SETCOLOR` 等指令修改后会一直保持，直到执行 `RESETCOLOR` 还原。当前文字颜色可用 `GETCOLOR` 获取，默认文字颜色可用 `GETDEFCOLOR` 获取。无参数，无错误分支。

## 用法

### RESETCOLOR
无参数。

```erb
SETCOLOR 255, 128, 0
PRINTL 这是橙色文字。
RESETCOLOR
PRINTL 这是默认颜色文字。
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:273` → `new RESETCOLOR_Instruction()`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:173`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1534`（`RESETCOLOR_Instruction`）

```text
类 RESETCOLOR_Instruction:
  构造: 参数构造器 = VOID（无参数）; 标志 = METHOD_SAFE | EXTENDED

  DoInstruction(exm, func, state):
    exm.Console.SetStringStyle(Config.ForeColor)   // 直接把文字样式重置为配置的默认前景色
```

## 备注

- ecd 文档把 RESETCOLOR 放在 SETCOLOR 小节内描述；实现上只是把控制台样式设回 `Config.ForeColor`，并不会清除 `SETCOLORBYNAME` 之外的特殊样式（如粗体斜体，那些由 `SETSTYLE` 控制）。
- 与 `SETBGCOLOR` 相对应的背景色还原命令是 `RESETBGCOLOR`，两者互不影响。
- Era-Chinese-Documentation 套件的 Command.md 仅覆盖 PRINT 系列，未收录本命令。
