# TOOLTIP_SETFONT

- **类别**：EE 扩展命令（Emuera 枚举成员；EE 扩展；两套中文文档未收录，语义据 EmueraEE readme + 源码）
- **签名**：`TOOLTIP_SETFONT <字体名（字符串表达式）>`
- **文档来源**：无中文文档收录；`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt:236`（「GSETFONTとTOOLTIP_SETFONTにも対応」——ttf/otf 字体功能说明）；`EmueraEE_changelog.txt:94`（v26「ツールチップ機能拡張命令追加」，未列命令名）；实现依据本仓库源码。

## 语义

设置扩展工具提示（自绘 tooltip）使用的字体名。属于「工具提示功能扩展」命令组（见 `TOOLTIP_EXTENSION.md`），**必须先 `TOOLTIP_CUSTOM 1` 打开自绘模式才可见效果**——否则 ToolTip 走系统默认绘制，本命令设置的字段不被使用。

- 只写入控制台字段 `tooltip_fontname`（默认值为 CONFIG 的主字体 `Config.FontName`），不立即重画画面；下一次 tooltip 弹出（`ToolTip_Popup` 量尺寸）与绘制（`ToolTip_Draw` 画文字）时生效。
- 字体名解析顺序（绘制端）：先在 `GlobalStatic.Pfc`（PrivateFontCollection，装载 Emuera 目录下 `font` 文件夹的 ttf/otf，`GlobalStatic.cs:48`）中按名称**精确匹配**；找不到则直接 `new Font(名称, 字号)`（GDI+ 找不到时回退系统默认字体，不报错）。
- 参数必填（`STR_EXPRESSION`，非 nullable）：一个参数都不给时在解析阶段报「引数が足りません」错误。
- 不校验字体是否存在；不改变字号（字号用 `TOOLTIP_SETFONTSIZE`）。

## 用法

### `TOOLTIP_SETFONT <字体名（字符串表达式）>`
- `<字体名>`：字体族名称字符串，如 `"ＭＳ ゴシック"`；把 ttf/otf 放进 Emuera 同目录的 `font` 文件夹后也可用其族名（对应 readme 的 ttf/otf 说明）。

```erb
TOOLTIP_CUSTOM 1
TOOLTIP_SETFONT "たぬゴ"
TOOLTIP_SETFONTSIZE 14
HTML_PRINT "<p><button title='带提示的按钮'>按钮</button></p>"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:375`（枚举 `TOOLTIP_SETFONT`，`#region EE`）；`Runtime/Script/Statements/FunctionIdentifier.cs:422`（`addFunction(FunctionCode.TOOLTIP_SETFONT, new TOOLTIP_SETFONT_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2933`（`TOOLTIP_SETFONT_Instruction`，`#region EE_TOOLTIP拡張` 自 `:2932`）；参数构建 `FunctionArgType.STR_EXPRESSION`（`Runtime/Script/Statements/ArgumentBuilder.cs:1451`，`argb` 登记于 `:184`）
- 消费端：`UI/Game/EmueraConsole.cs:1971`（`SetToolTipFontName`，字段 `:1962`）；绘制 `:1876-1906`（`ToolTip_Draw`，字体分支 `:1891-1905`）、量尺寸 `:1908-1933`（`ToolTip_Popup`，字体分支 `:1920-1932`）

```text
构造 TOOLTIP_SETFONT_Instruction:
    ArgBuilder = STR_EXPRESSION（必填 1 个字符串表达式）
    # 源码注释：//スキップ不可
    # 原 flag = IS_PRINT | IS_INPUT | EXTENDED 已注释，现行 flag = EXTENDED

DoInstruction(exm, func, state):
    fn = (ExpressionArgument)func.Argument
    exm.Console.SetToolTipFontName(fn.Term.GetStrValue(exm))
    # EmueraConsole.SetToolTipFontName(fn)（EmueraConsole.cs:1971）:
    #     tooltip_fontname = fn

绘制端 ToolTip_Draw（EmueraConsole.cs:1876，仅自绘模式挂接）:
    若 tooltip_img 且提示文字是整数且该 gID 已创建: 画 g 图并返回（字体不参与）
    遍历 GlobalStatic.Pfc.Families 找名称 == tooltip_fontname 的族:
        找到 → 用该族构造 Font(族, tooltip_fontsize) 后 TextRenderer.DrawText(..., tooltip_format)
    未找到 → new Font(tooltip_fontname, tooltip_fontsize) 同上绘制
```

## 备注

- 两套中文文档（ecd 与 Era-Chinese-Documentation）均未收录本命令；ecd「工具提示系」小节只有 `TOOLTIP_SETCOLOR`/`TOOLTIP_SETDELAY`/`TOOLTIP_SETDURATION`。
- readme 的证据仅说明「本字体功能也对 TOOLTIP_SETFONT 有效」，未给出参数格式；参数类型（单个必填字符串表达式）系据 `FunctionArgType.STR_EXPRESSION` 推定。
- 变更史：`EmueraEE_changelog.txt:94`（v26）「ツールチップ機能拡張命令追加」即含本命令（未逐个列名）；v36（`:39` 前后）加入 ttf/otf 字体支持。
- 与 `GSETFONT` 的分工：`GSETFONT` 设置 g 图文字（GDRAWTEXT）用字体，本命令设置 tooltip 用字体，两者共用同一套字体族解析（`Pfc` 优先）。
- 与 `TOOLTIP_IMG` 的交互：图像分支命中时直接画图，本命令设置的字体不参与该次绘制。
- 本命令只改字段、不刷新画面，且需 `TOOLTIP_CUSTOM 1`；详见 `TOOLTIP_CUSTOM.md`、`TOOLTIP_IMG.md`、`TOOLTIP_EXTENSION.md`。
