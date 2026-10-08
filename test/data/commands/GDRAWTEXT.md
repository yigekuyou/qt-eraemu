# GDRAWTEXT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（本仓库中实现为式中函数）
- **签名**：
  - `GDRAWTEXT <GraphicsID>, "<文本>"`
  - `GDRAWTEXT <GraphicsID>, "<文本>", <X坐标>, <Y坐标>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・GDRAWTEXT gID, "テキスト", X座標, Y座標」；`EmueraEE_changelog.txt`（EEv3 追加、EEv15 修正「未用 GSETFONT 时执行的异常崩溃」）。ecd 套件与 zh 套件均未收录本命令。

## 语义

在指定 Graphics 上描绘文本。坐标可省略（省略时为 `(0, 0)`）。配合 `GSETBRUSH` 指定的笔刷可改变文字的颜色与浓度（cARGB 为浓度 + 文字色的 8 位十六进制值）；配合 `GSETPEN` 可给文字描边；字体由 `GSETFONT` 指定（EE 扩展后第 4 参数还可指定粗体／斜体等样式）。

成功时返回 `1`，同时：`RESULT:0` 为 1、`RESULT:1` 为生成的文本图像宽度、`RESULT:2` 为高度（高度基本等于 `GSETFONT` 指定的值）。作为式中函数调用时返回值同样为 1，尺寸结果仍写入 `RESULT` 数组。绘制格式自 v7 起改为 `GenericTypographic`，使描画位置与 `PRINT` 实际显示一致。绘制方式为 `WINAPI` 时不能使用；Graphics 未创建时返回 0。

## 用法

### `GDRAWTEXT <GraphicsID>, <文本>{, <X>, <Y>}`
```erb
GCREATE 0, 300, 100
GSETFONT 0, "MS Gothic", 30
GSETBRUSH 0, 0xFFFF0000          ;红色半透明文字（ARGB：AA=浓度）
GDRAWTEXT 0, "こんにちは", 10, 20
PRINTVL RESULT:1                 ;文本图像宽度
PRINTVL RESULT:2                 ;文本图像高度
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:317`（`["GDRAWTEXT"] = new GraphicsDrawStringMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5568`（`GraphicsDrawStringMethod`，`#region EE_GDRAWTEXT`；`GetIntValue` 在 5600 行）；底层 `UI/Game/Image/GraphicsImage.cs:131`（`GDrawString(string, int, int)`）

```text
构造: ReturnType = 整数
      argumentTypeArrayEx = [ [int, string, int, int], OmitStart = 2 ]   # 2 或 4 个参数
      CanRestructure = false

GetIntValue(exm, arguments):
    if Config.TextDrawingMode == WINAPI:
        throw CodeEE(「该命令只能在 GDI+ 绘制方式下使用」)
    g = ReadGraphics(Name, exm, arguments, 0)
    if !g.IsCreated: return 0
    text = arguments[1].GetStrValue(exm)
    if arguments.Count == 2:
        g.GDrawString(text, 0, 0)
    else if arguments.Count == 4:
        p = ReadPoint(Name, exm, arguments, 2)
        g.GDrawString(text, p.X, p.Y)
    # 生成文本的尺寸测量
    bitmap = new Bitmap(16, 16)
    graphics = Graphics.FromImage(bitmap)
    font = g.Fnt                                  # GSETFONT 设置的字体
    if font == null:
        font = new Font(Config.FontName, 100, Console.StringStyle.FontStyle, Pixel)
    size = graphics.MeasureString(text, font, int.MaxValue, StringFormat.GenericTypographic)
    resultArray = exm.VEvaluator.RESULT_ARRAY     # 即 RESULT
    resultArray[1] = (long)size.Width
    resultArray[2] = (long)size.Height
    return 1

# GraphicsImage.GDrawString（实际描绘）
GDrawString(text, x, y):
    Load(); if g == null: throw NullReferenceException
    drawImgList = null
    usingFont = font ?? new Font(Config.FontName, 100, Console.StringStyle.FontStyle, Pixel)
    format = new StringFormat(GenericTypographic)
    emSize = usingFont.Height * FontFamily.GetEmHeight(style) / FontFamily.GetLineSpacing(style)
    gp = new GraphicsPath()
    gp.AddString(text, usingFont.FontFamily, (int)usingFont.Style, emSize, new Point(x, y), format)
    g.SmoothingMode = AntiAlias
    g.FillPath(brush ?? new SolidBrush(Config.ForeColor), gp)   # 填充 = GSETBRUSH 笔刷
    g.DrawPath(pen ?? new Pen(Config.ForeColor), gp)            # 描边 = GSETPEN 笔
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准，两者一致（RESULT:0/1/2 的赋值在 `Runtime/Script/Statements/Function/Creator.Method.cs:5626-5629`；v7 起 GenericTypographic 对应 `StringFormat.GenericTypographic`）。
- readme「GSETFONT cARGB と組み合わせることで…」一句与源码对照后应理解为 `GSETBRUSH`（浓度+颜色由笔刷决定），readme 写作 GSETBRUSH 的姊妹描述存在笔误空间；源码中填充用 `brush`（GSETBRUSH），描边用 `pen`（GSETPEN）。
- changelog EEv15 修正了「未用 GSETFONT 直接执行时异常崩溃」——对应源码中 `font == null` 时回退构造默认字体的分支；`GraphicsImage.GDrawString` 内同样有回退，但 `GSetFont` 为 null 时旧版会 NRE。
- 尺寸测量用一个 16x16 临时位图的 `MeasureString` 完成，与实际描绘用的 GraphicsPath 路径度量略有差异，宽度/高度是近似值。
