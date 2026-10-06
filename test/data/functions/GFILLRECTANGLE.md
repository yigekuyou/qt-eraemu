# GFILLRECTANGLE

- **类别**：式中函数（图像处理系；ecd/Command.md 按指令形式记载）
- **签名**：int GFILLRECTANGLE(int ID, int x, int y, int width, int height)
- **文档来源**：`ecd/Command.md`「### GFILLRECTANGLE `<ID>`, `<x>`, `<y>`, `<宽度>`, `<高度>`」（图像处理系小节）；ecd/Expression.md 未收录；zh 套件未收录

## 语义

在指定 ID 的 Graphics 上，用当前画刷填充由 `(x, y, width, height)` 指定的矩形。

- 仅当绘制方式（TextDrawingMode）为 `GRAPHICS` 或 `TEXTRENDERER` 时可用；绘制方式为 `WINAPI` 时抛出运行期错误。
- 填充颜色需要事先用 `GSETBRUSH` 指令为该 Graphics 指定，否则使用回退颜色填充（文档称"Emuera 配置中的字体颜色"；本仓库源码实际回退为配置的**背景色** `Config.BackColor`，见备注）。
- 处理成功时返回非 0（源码返回 1）；目标 Graphics 未创建（含已废弃）时返回 0，不做任何绘制。
- 参数不合适（ID 为负或过大、x/y 超出 int 范围、宽或高为 0 或超出 int 范围）时抛出运行期错误。

## 用法

### int GFILLRECTANGLE(int ID, int x, int y, int width, int height)
- `ID`：目标 Graphics 的编号（GCREATE 等创建时指定）。
- `x`、`y`：矩形左上角坐标。
- `width`、`height`：矩形的宽与高（不可为 0）。
- 返回值：成功 `1`；目标 Graphics 未创建时 `0`。
```erb
GCREATE 0, 200, 100
GSETBRUSH 0, 0xFF0000FF          ;不透明蓝
GFILLRECTANGLE 0, 10, 10, 100, 50
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:182`（`["GFILLRECTANGLE"] = new GraphicsFillRectangleMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6221`（`GraphicsFillRectangleMethod`）；辅助函数 `ReadGraphics`（`Runtime/Script/Statements/Function/Creator.Method.cs:5159`）、`ReadRectangle`（`:5215`）；底层 `GraphicsImage.GFillRectangle`（`UI/Game/Image/GraphicsImage.cs:215`）

```text
构造：返回类型 = long；参数 = [long ×5]；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛出 CodeEE（"××× 只能在 GDI+ 绘图模式（GRAPHICS/TEXTRENDERER）下使用"）
    g ← ReadGraphics(第 0 参):                # Creator.Method.cs:5159
        ID 为负 → CodeEE；ID > int.MaxValue → CodeEE
        返回 AppContents.GetGraphics(ID)      # 可能返回未创建的 GraphicsImage
    若 !g.IsCreated:
        返回 0                                # 未创建时静默失败，返回 0
    rect ← ReadRectangle(第 1~4 参):          # Creator.Method.cs:5215
        x、y 任一超出 int 范围 → CodeEE
        width、height 超出 int 范围或 == 0 → CodeEE
    g.GFillRectangle(rect):
        Load()                                # 确保位图就绪；g == null 时抛 NullReferenceException
        drawImgList ← null                    # 使绘制缓存失效
        若 brush != null: g.FillRectangle(brush, rect)
        否则: 用 new SolidBrush(Config.BackColor) 填充
    返回 1
```

## 备注

- ecd/Command.md 把 GFILLRECTANGLE 与 G 系图像指令一起按"指令"形式记载（小节标题 `<ID>`, `<x>`, …），但它在 `Runtime/Script/Statements/Function/Creator.cs` 注册为式中函数，可在表达式中调用并返回成功标志。本文档以式中函数形态为主。
- **文档与源码差异（回退填充色）**：文档称未用 `GSETBRUSH` 指定时"使用 Emuera 配置中的字体颜色绘制"；源码 `GraphicsImage.GFillRectangle`（GraphicsImage.cs:215）在 brush 为 null 时回退为 `new SolidBrush(Config.BackColor)`（背景色）。两边如实记录。
- 源码类上的 XML 注释写的是 `GFILLRECTANGLE(int ID, int cARGB, int x, y, w, h)`（6 参数、含颜色参数），与实际签名（5 参数、颜色来自画刷）不符，为过时注释。
- 与 `GSETCOLOR`（逐像素设置）相比填充整个矩形；文档在 GSETCOLOR 小节提示该类操作速度不快。
- zh 套件未收录本函数。
