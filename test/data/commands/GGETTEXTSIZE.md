# GGETTEXTSIZE

- **类别**：EE 扩展命令（命令 / 式中函数两用）
- **签名**：
  - `GGETTEXTSIZE <文本>, <字体名>, <字号>{, <字体样式>}`
- **文档来源**：EM+EE 在线文档「GGETTEXTSIZE」；`EmueraEE_readme.txt`「・GGETTEXTSIZE "テキスト", フォント名, フォントサイズ, フォントスタイル」条目；`ecd/Command.md` 未收录；zh 套件未收录。

## 语义

预测 `GDRAWTEXT` 以指定参数（文本、字体名、字号、可选字体样式）绘制时生成的图像尺寸。返回值为宽度（Width），同时把高度（Height）代入 `RESULT:1`。因此以行内函数形式调用时需要另行处理 `RESULT:1`，文档推荐使用命令形式。

字体样式为与 `SETFONT` 相同的 4 位整数：1=粗体、2=斜体、4=删除线、8=下线，可组合相加；省略时为常规样式。EE readme 提醒：FONTBOLD 相当的样式会使测得的尺寸略微偏大。仅在文本绘制方式为 GDI+（非 WINAPI）时可用。

EE v7 加入（changelog：`関数追加：GGETTEXTSIZE`）；EE 后续版本曾修复「一部分字体下 GDRAWTEXT 及 GGETTEXTSIZE 行为怪异」的问题。

## 用法

### `GGETTEXTSIZE <文本>, <字体名>, <字号>{, <字体样式>}`
- `<文本>`：字符串表达式，要测量的文本。
- `<字体名>`：字符串表达式，字体名。
- `<字号>`：数值表达式，以像素为单位的字号。
- `<字体样式>`：可省略的数值表达式，4 位样式标志（1=粗体 2=斜体 4=删除线 8=下线）。
- 结果：宽度作为命令返回值进入 `RESULT:0`，高度进入 `RESULT:1`。
```erb
@SYSTEM_TITLE
GGETTEXTSIZE "USA", "Arial", 150
PRINTFORML Width:{RESULT:0} Height:{RESULT:1}
GGETTEXTSIZE "日本", "ＭＳ Ｐゴシック", 150
PRINTFORML Width:{RESULT:0} Height:{RESULT:1}
WAIT
;输出示例：Width:308 Height:167 ／ Width:300 Height:150
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:321`（`["GGETTEXTSIZE"] = new GraphicsGetTextSizeMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5641`（`GraphicsGetTextSizeMethod`，`#region EE_GGETTEXTSIZE`）

```text
class GraphicsGetTextSizeMethod : FunctionMethod
    构造: ReturnType = long
          argumentTypeArrayEx = [{String, String, Int, Int}, OmitStart = 3]   # 第4参数可省略
          CanRestructure = false
    GetIntValue(exm, arguments):
        若 Config.TextDrawingMode == WINAPI: 抛出 CodeEE（仅 GDI+ 可用）
        text     = arguments[0].GetStrValue()
        fontname = arguments[1].GetStrValue()
        fontsize = arguments[2].GetIntValue()
        fs = FontStyle.Regular
        若 arguments.Count > 3:
            style = arguments[3].GetIntValue()
            若 style & 1: fs |= Bold
            若 style & 2: fs |= Italic
            若 style & 4: fs |= Strikeout
            若 style & 8: fs |= Underline
        fnt = new Font(fontname, fontsize, fs, GraphicsUnit.Pixel)
        graphics = Graphics.FromImage(new Bitmap(16, 16))     # 临时画布，仅用于测量
        size = graphics.MeasureString(text, fnt, int.MaxValue, StringFormat.GenericTypographic)
        RESULT_ARRAY[1] = (long)size.Height                   # 副作用：高度写入 RESULT:1
        返回 (long)size.Width                                 # 宽度作为返回值（命令形式下进 RESULT:0）
```

## 备注

- 文档（在线文档）称「预测 GDRAWTEXT 绘制时生成的 Width/Height，分别代入 RESULT:0/RESULT:1」，与实现一致：宽度为返回值、高度写 `RESULT:1`。
- EE readme 提示「FONTBOLD 相当的样式尺寸会略微变大」，源于 GDI+ MeasureString 对粗体的度量差异，非实现 bug。
- 实现中创建了 16x16 临时 Bitmap 且未显式释放（依赖 GC），属实现细节。
