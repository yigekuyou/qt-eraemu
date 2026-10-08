# TOOLTIP_SETFONTSIZE

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（Emuera 枚举成员；EE 扩展；两套中文文档未收录，语义据 EmueraEE readme + 源码）
- **签名**：`TOOLTIP_SETFONTSIZE <字号（数值表达式）>`
- **文档来源**：无中文文档收录；`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt:236`（「GSETFONTとTOOLTIP_SETFONTにも対応」为工具提示字体功能的总述，字号含在内）；`EmueraEE_changelog.txt:94`（v26「ツールチップ機能拡張命令追加」）；实现依据本仓库源码。参数类型与单位系据源码推定。

## 语义

设置扩展工具提示（自绘 tooltip）绘制文字时使用的字号。属于「工具提示功能扩展」命令组（见 `TOOLTIP_EXTENSION.md`），**需先 `TOOLTIP_CUSTOM 1` 打开自绘模式**才可见效果。

- 只写入控制台字段 `tooltip_fontsize`（默认值为 CONFIG 的主字号 `Config.FontSize`，字段类型 `long`），不立即重画；下一次 tooltip 弹出/绘制时生效。
- 字号用于 `new Font(tooltip_fontname, tooltip_fontsize)`（绘制、量尺寸两处都会新建 Font）：按 .NET `System.Drawing.Font(string, float)` 的语义解释为**磅（emSize，point）**，源码未做任何单位换算（推定：依 .NET API 定义）。
- 本命令自身不做范围校验。负值、0 等非法字号不会在此处报错，而是在绘制端构造 `Font` 时由 GDI+ 抛 `ArgumentException`（推定：依 .NET API 行为；源码无校验）。
- 参数必填（`INT_EXPRESSION`）：必须给一个数值表达式，缺少参数在解析阶段报错。

## 用法

### `TOOLTIP_SETFONTSIZE <字号（数值表达式）>`
- `<字号>`：希望使用的字号（磅）；字体的选择用 `TOOLTIP_SETFONT`，颜色用 `TOOLTIP_SETCOLOR`。

```erb
TOOLTIP_CUSTOM 1
TOOLTIP_SETFONTSIZE 18
HTML_PRINT "<p><button title='大号提示'>按钮</button></p>"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:376`（枚举 `TOOLTIP_SETFONTSIZE`，`#region EE`）；`Runtime/Script/Statements/FunctionIdentifier.cs:423`（`addFunction(FunctionCode.TOOLTIP_SETFONTSIZE, new TOOLTIP_SETFONTSIZE_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2949`（`TOOLTIP_SETFONTSIZE_Instruction`，`#region EE_TOOLTIP拡張` 自 `:2932`）；参数构建 `FunctionArgType.INT_EXPRESSION`（`Runtime/Script/Statements/ArgumentBuilder.cs:1328`，`argb` 登记于 `:182`）
- 消费端：`UI/Game/EmueraConsole.cs:1975`（`SetToolTipFontSize`，字段 `:1963`）；绘制 `:1876-1906`、量尺寸 `:1908-1933`

```text
构造 TOOLTIP_SETFONTSIZE_Instruction:
    ArgBuilder = INT_EXPRESSION（必填 1 个数值表达式）
    # 源码注释：//スキップ不可
    # 原 flag = IS_PRINT | IS_INPUT | EXTENDED 已注释，现行 flag = EXTENDED

DoInstruction(exm, func, state):
    fs = (ExpressionArgument)func.Argument
    exm.Console.SetToolTipFontSize(fs.Term.GetIntValue(exm))
    # EmueraConsole.SetToolTipFontSize(fs)（EmueraConsole.cs:1975）:
    #     tooltip_fontsize = fs      （long 字段，默认 Config.FontSize）

绘制/量尺寸端（EmueraConsole.cs:1876/1908，仅自绘模式挂接）:
    若 tooltip_img 且提示文字是整数且该 gID 已创建: 画 g 图并返回（字号不参与）
    否则:
        在 GlobalStatic.Pfc.Families 中找 tooltip_fontname 同名族
        用该族（或原始名称）构造 Font(名称, tooltip_fontsize)
        绘制 TextRenderer.DrawText(..., tooltip_format) / 量尺寸 TextRenderer.MeasureText(..., tooltip_format)
```

## 备注

- 两套中文文档均未收录本命令（ecd「工具提示系」只有 `TOOLTIP_SETCOLOR`/`TOOLTIP_SETDELAY`/`TOOLTIP_SETDURATION`）。
- readme 未单列本命令，字号语义出自 EM 文档站 Reference/TOOLTIP_EXTENSION 页（见 `TOOLTIP_EXTENSION.md` 的梳理）；「参数为单个数值表达式」据 `FunctionArgType.INT_EXPRESSION` 推定。
- 与 `TOOLTIP_SETFONT` 成对使用：字体名与字号分别设置，默认值取自 CONFIG 的主字体设置。
- 与 `TOOLTIP_IMG` 的交互：图像分支命中时字号不参与该次绘制；`TOOLTIP_IMG.md` 中 ToolTipSize 走图像宽高，文字度量被跳过。
