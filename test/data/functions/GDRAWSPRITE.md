# GDRAWSPRITE

- **类别**：式中函数（G 系图像处理指令，可作命令或函数使用）
- **签名**：int GDRAWSPRITE(`<ID>`, `<精灵名>`)
- **签名**：int GDRAWSPRITE(`<ID>`, `<精灵名>`, `<目标X>`, `<目标Y>`)
- **签名**：int GDRAWSPRITE(`<ID>`, `<精灵名>`, `<目标X>`, `<目标Y>`, `<目标宽度>`, `<目标高度>`)
- **签名**：int GDRAWSPRITE(`<ID>`, `<精灵名>`, `<目标X>`, `<目标Y>`, `<目标宽度>`, `<目标高度>`, `<颜色矩阵>`)
- **文档来源**：`ecd/Command.md`「### GDRAWSPRITE」四节（图像处理相关章节）；`ecd/Expression.md` 未收录（签名列表不含 G 系）；zh 套件未收录

## 语义

把指定名称的精灵（sprite，在 `resources` 文件夹的 CSV 中声明，或由 `SPRITECREATE` 创建）绘制到指定 ID 的 Graphics 上。

- 2 参形式：按精灵的基准尺寸（`DestBaseSize`）画到 (0,0)。
- 4 参形式：指定绘制位置 (destX, destY)。
- 6 参形式：指定目标矩形，可将精灵放大/缩小绘制。
- 7 参形式：再指定 5×5 数值数组作为颜色矩阵（元素先 ÷256 使用，对角线全 256 为单位矩阵）。

处理成功返回 `1`；目标 Graphics 未创建、或指定名称的精灵不存在/未创建时返回 `0`。指定动画精灵时绘制运行时的其中一帧。精灵尺寸可用 `SPRITEWIDTH(imgName)`、`SPRITEHEIGHT(imgName)` 获取。G 系指令要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报错。

## 用法

### int GDRAWSPRITE(ID, sprName)
- ID：目标 Graphics ID。
- sprName：精灵名（字符串）。
```erb
GCREATE 0, 200, 100
GDRAWSPRITE 0, "title_logo"
```
### int GDRAWSPRITE(ID, sprName, destX, destY)
- destX、destY：目标上的绘制位置。
```erb
GDRAWSPRITE 0, "title_logo", 10, 20
```
### int GDRAWSPRITE(ID, sprName, destX, destY, destW, destH)
- destW、destH：绘制尺寸（0 报错），实现缩放。
```erb
GDRAWSPRITE 0, "title_logo", 0, 0, 100, 50
```
### int GDRAWSPRITE(ID, sprName, destX, destY, destW, destH, cm)
- cm：5×5 以上整数二维（或三维）数组变量，颜色矩阵（先 ÷256）。
```erb
DIM CM, 5, 5
FOR i, 0, 4
    CM:i:i = 256
NEXT
GDRAWSPRITE 0, "title_logo", 0, 0, 100, 50, CM
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:183`（`["GDRAWSPRITE"] = new GraphicsDrawSpriteMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6368`（`GraphicsDrawSpriteMethod`——类注释仍是旧名 GDRAWCIMG）；绘制核心 `UI/Game/Image/GraphicsImage.cs:238`、`:296`（`GDrawCImg`）

```text
GraphicsDrawSpriteMethod:
构造：返回类型 = long；
    参数表 4 种：[int, str] / [int, str, int, int] /
                [int, str, int, int, int, int, RefInt2D(可常量引用)] /
                [int, str, int, int, int, int, RefInt3D(可常量引用)]，
    第 7 参（颜色矩阵）可省略（OmitStart = 6）；
    CanRestructure = false，HasUniqueRestructure = true（第 7 参不可折叠）。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI: 抛 CodeEE（仅 GDI+ 绘制方式可用）
    dest = ReadGraphics(args[0])
    若 !dest.IsCreated: 返回 0
    imgname = args[1].GetStrValue(exm)
    img = AppContents.GetSprite(imgname)
    若 img == null 或 !img.IsCreated: 返回 0     ; 精灵不存在/未创建

    destRect = Rectangle(0, 0, img.DestBaseSize.Width, img.DestBaseSize.Height)
    switch (args.Count):
        2:  dest.GDrawCImg(img, destRect)；返回 1
        4:  p = ReadPoint(args[2], args[3])
            destRect.X = p.X；destRect.Y = p.Y
            dest.GDrawCImg(img, destRect)；返回 1
        6:  destRect = ReadRectangle(args[2..5])   ; 宽高 0 → CodeEE
            dest.GDrawCImg(img, destRect)；返回 1
        7:  destRect = ReadRectangle(args[2..5])
            cm = ReadColormatrix(args[6])          ; 5×5 窗口，÷256；越界 → CodeEE
            dest.GDrawCImg(img, destRect, cm)；返回 1

GraphicsImage.GDrawCImg(img, rect[, cm]):
    Load()；g 判空
    无 cm：更新 drawImgList（SpriteG/SpriteF 的图元列表合并或置空，
           SpriteF 列表超过 50 个时置空以防膨胀），随后 img.GraphicsDraw(g, rect)
    有 cm：drawImgList = null；用 ColorMatrix 构造 ImageAttributes 后
           img.GraphicsDraw(g, rect, imageAttributes)
```

## 备注

- 文档 4 种签名与源码的 4 种参数表一一对应；「可应用颜色矩阵」实现在有 cm 时绕过 drawImgList 优化（`drawImgList = null`），颜色矩阵绘制不支持图元列表缓存。
- 源码类注释写作 `GDRAWCIMG(int ID, str imgName)…`，说明本函数由 EE 的 GDRAWCIMG 更名而来（类名 GraphicsDrawSpriteMethod 保持不变）；文档以 GDRAWSPRITE 为准。
- 文档「指定动画精灵时，会绘制运行时的其中一帧」未在实现层显式处理，由 ASprite/帧机制自然处理。
- 精灵不存在时返回 `0` 而不是报错，这一点与文档一致。
