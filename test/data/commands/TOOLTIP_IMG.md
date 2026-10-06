# TOOLTIP_IMG

- **类别**：EE 扩展命令
- **签名**：
  - `TOOLTIP_IMG <数值表达式>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`（・TOOLTIP_IMG，约 244 行）；`EmueraEE_changelog.txt:6`「TOOLTIP_IMG追加」。ecd 文档未收录本命令（其「工具提示系」小节只有 TOOLTIP_SETCOLOR/SETDELAY/SETDURATION）；zh 套件未收录。

## 语义

允许在按钮的工具提示中显示图像（g 图）。参数非 0 时开启该功能；开启前提是工具提示扩展（`TOOLTIP_CUSTOM 非0`）也已开启。开启后，若某按钮的提示文字（`title` 属性内容）本身是一个整数，则把该整数当作 g 图 ID，直接把这个 g 的位图画到工具提示里（工具提示尺寸随之变为 g 的宽高）；若该 g 尚未创建（`GCREATE` 过）或提示文字不是数字，则照旧按普通文字显示。参数 0 时关闭。

## 用法

### `TOOLTIP_IMG <数值表达式>`
- `<数值表达式>`：非 0 开启、0 关闭。需与 `TOOLTIP_CUSTOM 1` 配合。
```erb
GCREATE 100, 64, 64
;往 g100 画一个图标（省略）
TOOLTIP_CUSTOM 1
TOOLTIP_IMG 1
HTML_PRINT "<p><img src='100'> <button title='100'>指向 g100 的按钮</button></p>"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:379`（enum `TOOLTIP_IMG`）；`Runtime/Script/Statements/FunctionIdentifier.cs:426`（`addFunction(FunctionCode.TOOLTIP_IMG, new TOOLTIP_IMG_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3000`（`TOOLTIP_IMG_Instruction`，`#region EE_TOOLTIP拡張`）；显示逻辑在 `UI/Game/EmueraConsole.cs:1878`（`ToolTip_Draw`）与 `:1910`（`ToolTip_Popup`）

```text
构造:
    ArgBuilder = 单个 INT_EXPRESSION 参数
    flag = EXTENDED

DoInstruction(exm, func, state):
    exm.Console.SetToolTipImg(参数.GetIntValue(exm) != 0)
    # EmueraConsole.SetToolTipImg(bool)（EmueraConsole.cs:1983）：
    #   tooltip_img = b   （实例字段，默认 false）

ToolTip_Popup（弹出时决定尺寸）:
    若 tooltip_img 且 int.TryParse(提示文字, out i):
        g = FunctionMethodCreator.ReadGraphics(i)
        若 g.IsCreated: e.ToolTipSize = new Size(g.Width, g.Height); 返回
    否则按字体 MeasureText 计算文字尺寸

ToolTip_Draw（绘制）:
    若 tooltip_img 且 int.TryParse(提示文字, out i):
        g = ReadGraphics(i)
        若 g.IsCreated: e.Graphics.DrawImage(g.Bitmap, 0, 0); 返回
    否则走普通文字绘制（背景、边框、tooltip_fontname/size/format）
```

## 备注

- Draw/Popup 事件只在 `TOOLTIP_CUSTOM 1`（`CustomToolTip(true)` 设置 `ToolTip.OwnerDraw = true`）时挂接，因此**不先开扩展本命令完全无效**，与 readme「ツールチップ拡張もオンになっている必要がある」一致。
- readme 说「gIDが生成されてない、文字列が数値じゃない場合はそのまま文字列として表示」，与源码 `IsCreated`/`TryParse` 两个回退分支一致。
- 文档只说「非0でオンになる」，源码为严格布尔化（`!= 0`）。
- 本命令只设置全局开关，不指定具体 gID；gID 由按钮的提示文字本身给出。
