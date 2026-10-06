# GDRAWG

- **类别**：式中函数（G 系图像处理指令，可作命令或函数使用）
- **签名**：int GDRAWG(`<目标ID>`, `<源ID>`, `<目标X>`, `<目标Y>`, `<目标宽度>`, `<目标高度>`, `<源X>`, `<源Y>`, `<源宽度>`, `<源高度>`)
- **签名**：int GDRAWG(`<目标ID>`, `<源ID>`, `<目标X>`, `<目标Y>`, `<目标宽度>`, `<目标高度>`, `<源X>`, `<源Y>`, `<源宽度>`, `<源高度>`, `<颜色矩阵>`)
- **文档来源**：`ecd/Command.md`「### GDRAWG `<目标ID>`, `<源ID>`, …」「### GDRAWG `<目标ID>`, `<源ID>`, …, `<颜色矩阵>`」（图像处理相关章节，两节）；`ecd/Expression.md` 未收录（签名列表不含 G 系）；zh 套件未收录

## 语义

把源 Graphics（srcID）的指定区域绘制到目标 Graphics（ID）的指定位置，可同时缩放（源矩形与目标矩形尺寸不同时按比例缩放）。

第 1 参数是绘制目标 dest 的 Graphics ID；第 2 参数是绘制源 src 的 Graphics ID；第 3～6 参数（destX、destY、destWidth、destHeight）指定目标上的位置和尺寸；第 7～10 参数（srcX、srcY、srcWidth、srcHeight）指定源上的位置和尺寸。宽高为 0 会报错（ReadRectangle 检查）。

处理成功返回 `1`；目标或源的 Graphics 未创建时返回 `0`。目标与源可以是同一个 Graphics。第 11 参数可选，指定 5×5 数值二维（或三维）数组作为颜色矩阵 `cm`：所有元素先除以 256 再传给 .NET 的 `ColorMatrix` 类，对角线全为 256 的 5×5 矩阵即单位矩阵。

G 系指令要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报错。

## 用法

### int GDRAWG(destID, srcID, destX, destY, destW, destH, srcX, srcY, srcW, srcH)
- destID：绘制目标 Graphics ID。
- srcID：绘制源 Graphics ID。
- destX、destY：绘制到目标上的位置。
- destW、destH：绘制到目标上的尺寸（0 报错）。
- srcX、srcY、srcW、srcH：源上被复制的矩形（宽高 0 报错）。
- 返回值：成功 `1`；dest/src 未创建 `0`。
```erb
GCREATE 0, 200, 100
GCREATE 1, 100, 100
; 把 Graphics1 整张缩小一半画到 Graphics0 的 (50,0) 处
GDRAWG 0, 1, 50, 0, 100, 100, 0, 0, 100, 100
```
### int GDRAWG(destID, srcID, destX, destY, destW, destH, srcX, srcY, srcW, srcH, cm)
- cm：5×5 以上的整数二维（或三维）数组变量，作为颜色矩阵；元素值先 ÷256 后使用。
```erb
DIM CM, 5, 5
; 对角线 256 的矩阵 = 单位矩阵（原样绘制）
FOR i, 0, 4
    CM:i:i = 256
NEXT
GDRAWG 0, 1, 0, 0, 100, 100, 0, 0, 100, 100, CM
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:185`（`["GDRAWG"] = new GraphicsDrawGMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6247`（`GraphicsDrawGMethod`）；颜色矩阵读取 `Runtime/Script/Statements/Function/Creator.Method.cs:5240`（`ReadColormatrix`）；绘制核心 `UI/Game/Image/GraphicsImage.cs:315`、`:332`（`GDrawG`）

```text
GraphicsDrawGMethod:
构造：返回类型 = long；
    参数表 [7×int + RefInt2D(可常量引用)] 或 [7×int + RefInt3D(可常量引用)]，
    OmitStart = 10（第 11 参颜色矩阵可省略）；
    CanRestructure = false，HasUniqueRestructure = true（第 11 参不可折叠为常量）。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI: 抛 CodeEE（仅 GDI+ 绘制方式可用）
    dest = ReadGraphics(args[0])   ; 目标 ID 检查，负值等报错
    若 !dest.IsCreated: 返回 0
    src  = ReadGraphics(args[1])
    若 !src.IsCreated: 返回 0
    destRect = ReadRectangle(args[2..5])   ; (destX, destY, destW, destH)；越 int 范围或宽高=0 → CodeEE
    srcRect  = ReadRectangle(args[6..9])   ; (srcX, srcY, srcW, srcH)；同上
    若 args.Count == 10 或 args[10] == null:
        dest.GDrawG(src, destRect, srcRect)
            ; Load(); drawImgList = null
            ; g.DrawImage(src.GetBitmap(), destRect, srcRect, GraphicsUnit.Pixel)
        返回 1
    否则:
        cm = ReadColormatrix(args[10])
            ; 取数组变量中自给定下标起的 5×5 窗口；
            ; 下标越界（不足 5×5）→ CodeEE（颜色矩阵不合适）
            ; cm[x][y] = array[..] / 256f
        dest.GDrawG(src, destRect, srcRect, cm)
            ; 构造 ColorMatrix + ImageAttributes
            ; g.DrawImage(src, destRect, srcRect.X, srcRect.Y, srcRect.W, srcRect.H,
            ;             GraphicsUnit.Pixel, imageAttributes)
        返回 1

UniqueRestructure:
    逐参 Restructure；但第 11 参（颜色矩阵数组）只 Restructure 不折叠，防止把数组引用常量化。
```

## 备注

- 文档「5x5 以上的二维数值数组」与源码一致：实现支持二维和三维整数数组变量（两个 ArgTypeList 分支），从给定的起始下标处读取 5×5 窗口；「先除以 256」对应 `cm[x][y] = array[...] / 256f`；「对角线全 256 即单位矩阵」正确。
- 文档「绘制目标与绘制源的 Graphics 即使相同也可以执行」：源码对相同 ID 也没有限制（src.GetBitmap() 取出位图后 DrawImage，自绘可行）。
- 文档说「目标或源未创建等情况下返回 0」：源码只有「未创建」返回 0；矩形越界不检查（交给 GDI+ 裁剪），宽高为 0 则在 ReadRectangle 报错。
