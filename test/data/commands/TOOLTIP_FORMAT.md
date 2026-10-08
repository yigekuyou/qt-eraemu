# TOOLTIP_FORMAT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（Emuera 枚举成员；EE 扩展；两套中文文档未收录，语义据 EM 文档站 TOOLTIP_EXTENSION 页转述与源码）
- **签名**：`TOOLTIP_FORMAT <TextFormatFlags 数值表达式>`
- **文档来源**：无中文文档收录；语义要点（参数为 C# `System.Windows.Forms.TextFormatFlags` 枚举值）转引自同组文档 `TOOLTIP_EXTENSION.md:28`；`EmueraEE_changelog.txt:94`（v26「ツールチップ機能拡張命令追加」）；实现依据本仓库源码。

## 语义

设置扩展工具提示（自绘 tooltip）绘制/度量文字时使用的 .NET 排版标志。

- 只写入控制台字段 `tooltip_format`（类型 `TextFormatFlags`，默认 0），不立即重画；下一次 tooltip 弹出（`TextRenderer.MeasureText`）与绘制（`TextRenderer.DrawText`）时生效。
- 参数是 `TextFormatFlags` 的**位组合整数**（该枚举带 `[Flags]`），例如「允许换行」「单行」「不解析 `&` 前缀」「末尾省略号」等都是位标志，可相加或按位或组合。本仓库只是把它强转后原样传给 GDI+ 的 `TextRenderer`，未做任何解释、校验或容错。
- 常用成员及其数值（**非本仓库自定义**，取自 .NET 官方文档 `System.Windows.Forms.TextFormatFlags`）：
  - `WordBreak` = 16（0x10）：允许在词边界换行；
  - `SingleLine` = 32（0x20）：单行输出；
  - `NoPrefix` = 2048（0x800）：不把 `&` 当作助记符前缀（提示文字含 `&` 时建议加上，本仓库其它绘制路径统一使用 `TextFormatFlags.NoPrefix`，见 `UI/Game/ConsoleStyledString.cs:141`）；
  - `EndEllipsis` = 32768、`PathEllipsis` = 16384、`WordEllipsis` = 262144：超长时以省略号截断的不同策略；
  - `HorizontalCenter` = 1、`Right` = 2、`VerticalCenter` = 4、`Bottom` = 8：对齐方式；
  - 默认值 0 表示 `Left | Top | GlyphOverhangPadding`（均为 0）。
- 参数必填（`INT_EXPRESSION`）：必须给一个数值表达式，缺少参数在解析阶段报错。
- 对图像分支无效：`TOOLTIP_IMG` 命中图像时该次绘制/度量完全不经过 `TextRenderer`，本标志无作用。

## 用法

### `TOOLTIP_FORMAT <TextFormatFlags 数值表达式>`
- `<数值表达式>`：`TextFormatFlags` 位组合整数。

```erb
TOOLTIP_CUSTOM 1
;允许换行 + 不解析 & 前缀：WordBreak(16) 与 NoPrefix(2048) 的按位或
TOOLTIP_FORMAT 16 | 2048
TOOLTIP_SETFONTSIZE 12
HTML_PRINT "<p><button title='很长的提示文字，可自动换行'>按钮</button></p>"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:378`（枚举 `TOOLTIP_FORMAT`，`#region EE`）；`Runtime/Script/Statements/FunctionIdentifier.cs:425`（`addFunction(FunctionCode.TOOLTIP_FORMAT, new TOOLTIP_FORMAT_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2984`（`TOOLTIP_FORMAT_Instruction`，`#region EE_TOOLTIP拡張` 自 `:2932`）；参数构建 `FunctionArgType.INT_EXPRESSION`（`Runtime/Script/Statements/ArgumentBuilder.cs:1328`，`argb` 登记于 `:182`）
- 消费端：`UI/Game/EmueraConsole.cs:1979`（`SetToolTipFormat`，字段 `:1964`）；`TextRenderer.MeasureText(..., tooltip_format)` 于 `:1930`、`TextRenderer.DrawText(..., tooltip_format)` 于 `:1897` 与 `:1904`

```text
构造 TOOLTIP_FORMAT_Instruction:
    ArgBuilder = INT_EXPRESSION（必填 1 个数值表达式）
    # 源码注释：//スキップ不可
    # 原 flag = IS_PRINT | IS_INPUT | EXTENDED 已注释，现行 flag = EXTENDED

DoInstruction(exm, func, state):
    i = (ExpressionArgument)func.Argument
    exm.Console.SetToolTipFormat(i.Term.GetIntValue(exm))
    # EmueraConsole.SetToolTipFormat(f)（EmueraConsole.cs:1979）:
    #     tooltip_format = (TextFormatFlags)f     （无范围/合法性校验）

绘制/量尺寸端（EmueraConsole.cs:1876/1908，仅自绘模式挂接）:
    若 tooltip_img 且提示文字是整数且该 gID 已创建: 画 g 图并返回（tooltip_format 不参与）
    否则:
        TextRenderer.DrawText(e.Graphics, 提示文字, Font(tooltip_fontname, tooltip_fontsize),
                              区域, 前景色, 背景色, tooltip_format)      # :1897 / :1904
        TextRenderer.MeasureText(提示文字, 同字体, new Size(int.MaxValue, int.MaxValue), tooltip_format)  # :1930
```

## 备注

- 两套中文文档均未收录本命令；「参数是 TextFormatFlags」这一要点来自 `TOOLTIP_EXTENSION.md:28` 对 EM 文档站页面的转述，本文件补全实现细节。
- 源码对参数不做任何范围校验：传入负数或超出枚举定义范围的位组合不会被拒绝，会原样进入 `TextRenderer`（推定：由 GDI+ 忽略未知位）。
- 与 `TOOLTIP_IMG` 的交互：图像分支命中时本标志不参与绘制与度量。
- 与 `TOOLTIP_SETFONT`/`TOOLTIP_SETFONTSIZE` 同属字体度量组：三者共同决定「文字路径」的绘制方式，且都需 `TOOLTIP_CUSTOM 1` 才生效。
- 同一组内的 `TOOLTIP_SETCOLOR`/`TOOLTIP_SETDELAY`/`TOOLTIP_SETDURATION` 语义有 ecd 文档（见各文档）；本命令没有中文文档依据。
