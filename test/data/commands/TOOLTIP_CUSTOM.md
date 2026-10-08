# TOOLTIP_CUSTOM

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（Emuera 枚举成员；EE 扩展；两套中文文档未收录，语义据 EM 文档站 TOOLTIP_EXTENSION 页转述与源码）
- **签名**：`TOOLTIP_CUSTOM <数值表达式>`
- **文档来源**：无中文文档收录；`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt:227-228`（「・ツールチップ機能拡張 詳しくは→https://evilmask.gitlab.io/emuera.em.doc/Reference/TOOLTIP_EXTENSION/」）；`EmueraEE_changelog.txt:94`（v26「ツールチップ機能拡張命令追加」）；语义要点（非 0 开启、0 关闭）转引自同组文档 `TOOLTIP_EXTENSION.md:24`，实现依据本仓库源码。

## 语义

工具提示扩展的**总开关**：把 WinForms ToolTip 切换到自绘（OwnerDraw）模式。

- 参数非 0：开启扩展。挂接 `Draw`/`Popup` 两个事件回调，并置 `OwnerDraw = true`；此后 tooltip 的颜色（`TOOLTIP_SETCOLOR`）、延迟（`TOOLTIP_SETDELAY`）、持续（`TOOLTIP_SETDURATION`）、字体（`TOOLTIP_SETFONT`/`TOOLTIP_SETFONTSIZE`）、排版（`TOOLTIP_FORMAT`）、图像（`TOOLTIP_IMG`）才真正生效。
- 参数为 0：关闭扩展。退订两个事件并把 `OwnerDraw` 置回 false，恢复系统默认绘制（上述字段仍留在内存中，但不被使用；重新开启后原值继续有效）。
- 重复开启安全：仅当 `OwnerDraw` 还是 false 时才挂事件，不会重复订阅（源码 `else if (!window.ToolTip.OwnerDraw)`）。
- 本命令不改动工具提示的附加内容（`title`），只改绘制方式；如何让按钮带 tooltip 见 `HTML_PRINT.md`（`<button title='...'>`）。
- 参数必填（`INT_EXPRESSION`）：必须给一个数值表达式；源码按「是否等于 0」严格布尔化，非 0 一律视为开启。

## 用法

### `TOOLTIP_CUSTOM <数值表达式>`
- `<数值表达式>`：非 0 开启自绘扩展，0 关闭。

```erb
TOOLTIP_CUSTOM 1
TOOLTIP_SETCOLOR 0xFFFFFF, 0x404040
TOOLTIP_SETFONT "ＭＳ ゴシック"
TOOLTIP_SETFONTSIZE 12
HTML_PRINT "<p><button title='说明文字'>按钮</button></p>"
;...
TOOLTIP_CUSTOM 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:377`（枚举 `TOOLTIP_CUSTOM`，`#region EE`）；`Runtime/Script/Statements/FunctionIdentifier.cs:424`（`addFunction(FunctionCode.TOOLTIP_CUSTOM, new TOOLTIP_CUSTOM_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2965`（`TOOLTIP_CUSTOM_Instruction`，`#region EE_TOOLTIP拡張` 自 `:2932`）；参数构建 `FunctionArgType.INT_EXPRESSION`（`Runtime/Script/Statements/ArgumentBuilder.cs:1328`，`argb` 登记于 `:182`）
- 控制台：`UI/Game/EmueraConsole.cs:1935`（`CustomToolTip(bool)`）

```text
构造 TOOLTIP_CUSTOM_Instruction:
    ArgBuilder = INT_EXPRESSION（必填 1 个数值表达式）
    # 源码注释：//スキップ不可
    # 原 flag = IS_PRINT | IS_INPUT | EXTENDED 已注释，现行 flag = EXTENDED

DoInstruction(exm, func, state):
    b = (ExpressionArgument)func.Argument
    if b.Term.GetIntValue(exm) == 0:
        exm.Console.CustomToolTip(false)
    else:
        exm.Console.CustomToolTip(true)

EmueraConsole.CustomToolTip(b)（EmueraConsole.cs:1935）:
    if !b:
        window.ToolTip.Draw  -= ToolTip_Draw
        window.ToolTip.Popup -= ToolTip_Popup
    else if !window.ToolTip.OwnerDraw:      # 重复开启时不重复订阅
        window.ToolTip.Draw  += ToolTip_Draw
        window.ToolTip.Popup += ToolTip_Popup
    window.ToolTip.OwnerDraw = b            # 注意：这一行无条件执行
```

## 备注

- 两套中文文档均未收录本命令；其语义（开启自绘扩展）散见于 `TOOLTIP_EXTENSION.md:24` 的转述与 `TOOLTIP_IMG.md` 的说明，本文件补全实现细节。
- 本命令是工具提示扩展组的**前置条件**：`TOOLTIP_IMG.md` 已确认「Draw/Popup 只在 `TOOLTIP_CUSTOM 1` 时挂接，不先开扩展则 TOOLTIP_IMG 完全无效」；字体/颜色/格式同理。
- 注意 `OwnerDraw = b` 在函数末尾无条件执行：关闭时先退订再置 false，因此关闭后即使字段仍保留旧值，也不会再走自绘路径。
- 与 `TOOLTIP_SETCOLOR.md`、`TOOLTIP_SETDELAY`/`TOOLTIP_SETDURATION`（有独立文档）配合：它们只写字段，都属于「开了扩展才有意义」的一组。
- 命名中的 CUSTOM 指「自定义绘制」，不是「自定义提示内容」——提示文本来自按钮的 `title` 属性（`UI/Game/HtmlManager.cs:1310` 解析）。
