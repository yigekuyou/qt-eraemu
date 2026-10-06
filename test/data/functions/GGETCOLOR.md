# GGETCOLOR

- **类别**：式中函数（图像处理系；ecd/Command.md 按指令形式记载）
- **签名**：int GGETCOLOR(int ID, int x, int y)
- **文档来源**：`ecd/Command.md`「### GGETCOLOR `<ID>`, `<x>`, `<y>`」（图像处理系小节）；ecd/Expression.md 未收录；zh 套件未收录

## 语义

以 `0xAARRGGBB` 形式的整数值取得指定 ID 的 Graphics 中指定位置 (x, y) 的像素颜色（含 Alpha 通道）。

- 仅当绘制方式（TextDrawingMode）为 `GRAPHICS` 或 `TEXTRENDERER` 时可用；`WINAPI` 模式下抛出运行期错误。
- Graphics 未创建或已废弃，或者 x、y 在图像之外时返回 `-1`。
- 请注意，G 系指令中只有该函数在失败时返回 `-1` 而不是 0；当取得"黑色且完全不透明度 0（全透明黑）"位置的颜色时返回 0，这是正常取色结果而不是失败。
- ID 为负或过大、x/y 超出 int 范围时抛出运行期错误。

## 用法

### int GGETCOLOR(int ID, int x, int y)
- `ID`：目标 Graphics 的编号。
- `x`、`y`：像素坐标。
- 返回值：`0xAARRGGBB` 整数（0 ~ 4294967295）；失败时 `-1`。
```erb
GCREATE 0, 100, 100
GSETCOLOR 0, 0xFF00FF00, 50, 50     ;在 (50,50) 写入不透明绿
C = GGETCOLOR(0, 50, 50)
PRINTFORML R={C >> 16 & 0xFF} G={C >> 8 & 0xFF} B={C & 0xFF} A={C >> 24 & 0xFF}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:175`（`["GGETCOLOR"] = new GraphicsGetColorMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5373`（`GraphicsGetColorMethod`）；辅助函数 `ReadGraphics`（`:5159`）、`ReadPoint`（`:5199`）；底层 `GraphicsImage.GGetColor`（`UI/Game/Image/GraphicsImage.cs:538`）

```text
构造：返回类型 = long；参数 = [long ×3]；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛出 CodeEE（"××× 只能在 GDI+ 绘图模式下使用"）
    g ← ReadGraphics(第 0 参):                # ID 为负或 > int.MaxValue → CodeEE
    若 !g.IsCreated:
        返回 -1                               # 与多数 G 系不同，失败返回 -1
    p ← ReadPoint(第 1~2 参):                 # x、y 任一超出 int 范围 → CodeEE
    若 p.X < 0 或 p.X >= g.Width 或 p.X < 0 或 p.Y >= g.Height:
        返回 -1                               # 注意：源码把 p.X < 0 写了两次，未检查 p.Y < 0
    c ← g.GGetColor(p.X, p.Y):                # Bitmap.GetPixel(x, y)
    返回 c.ToArgb() & 0xFFFFFFFFL             # 转为无符号 32 位，即 0xAARRGGBB
```

## 备注

- ecd/Command.md 把 GGETCOLOR 与 G 系图像指令一起按"指令"形式记载，但它在 `Runtime/Script/Statements/Function/Creator.cs` 注册为式中函数，在表达式中调用。本文档以式中函数形态为主。
- **源码缺陷**：越界判断 `if (p.X < 0 || p.X >= g.Width || p.X < 0 || p.Y >= g.Height)` 中 `p.X < 0` 重复出现，`p.Y < 0` 从未被检查。按文档"y 在图像之外时返回 -1"，负的 y 理应返回 -1，但源码会把负 y 直接传给 `Bitmap.GetPixel`，触发运行期异常（ArgumentOutOfRangeException 路径未被本函数捕获）。文档与源码在此不一致，两边如实记录。
- 失败返回 `-1`（其余 G 系多数返回 0）：文档与源码一致；全透明黑返回 0 的说明也来自文档（源码 `ToArgb() & 0xFFFFFFFF` 对透明黑恰为 0）。
- 相对地，`GSETCOLOR` 为逐像素写入，文档提示与 GGETCOLOR 配合逐点改写整张大图无法在实用时间内完成。
- zh 套件未收录本函数。
