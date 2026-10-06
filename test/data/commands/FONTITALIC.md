# FONTITALIC

- **类别**：命令
- **签名**：`FONTITALIC`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md`「文字样式相关」`### FONTITALIC`／`### FONTREGULAR` 小节（三者共用一段说明）；`Era-Chinese-Documentation`（zh 套件）未收录该命令

## 语义

把当前显示文字样式追加为「倾斜（Italic）」。它与 `FONTBOLD` 一样只叠加一种属性，可与加粗同时生效（即 `FONTBOLD` 之后调用 `FONTITALIC` 得到加粗＋倾斜）。要一次性清除样式请用 `FONTREGULAR`，要按位组合设置请用 `FONTSTYLE`。无参数、无返回值，只影响之后的 PRINT 系输出，直到再次改变样式为止。

## 用法

### FONTITALIC
无参数。

```erb
FONTBOLD
FONTITALIC
PRINTL 加粗＋倾斜的文字
FONTREGULAR
PRINTL 恢复普通样式
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:281`（`new FONTITALIC_Instruction()`，flag = METHOD_SAFE | EXTENDED；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:181`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1633`（`FONTITALIC_Instruction`，VOID 参数）

```text
构造：ArgBuilder = VOID（无参数）；flag = METHOD_SAFE | EXTENDED

DoInstruction(exm, func, state):
    若 当前操作系统不是 Windows:
        直接 return（什么都不做）
    否则:
        exm.Console.SetStringStyle(当前样式.FontStyle | FontStyle.Italic)
        # 在现有字体样式上按位或追加"倾斜"位，保留加粗等其它位
```

其中 `SetStringStyle` 定义于 `UI/Game/EmueraConsole.Print.cs:91`，即 `userStyle.FontStyle = fs`。

## 备注

- 与源码的差异：`FONTBOLD`、`FONTITALIC` 的实现在非 Windows 平台上会直接 `return`，什么都不做；而 `FONTSTYLE`／`FONTREGULAR` 没有这层平台判断。这意味着在本仓库于非 Windows 环境下 `FONTITALIC` 实际无效。
- ecd 文档中 `FONTBOLD`/`FONTITALIC` 两个小节为空标题，说明文字统一放在 `FONTREGULAR` 小节下。
- 无参数版无法单独取消倾斜，取消须用 `FONTREGULAR` 或 `FONTSTYLE`。
