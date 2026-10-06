# GDRAWGWITHMASK

- **类别**：式中函数（G 系图像处理指令，可作命令或函数使用）
- **签名**：int GDRAWGWITHMASK(`<目标ID>`, `<源ID>`, `<掩码ID>`, `<目标X>`, `<目标Y>`)
- **文档来源**：`ecd/Command.md`「### GDRAWGWITHMASK `<目标ID>`, `<源ID>`, `<掩码ID>`, `<目标X>`, `<目标Y>`」（图像处理相关章节）；`ecd/Expression.md` 未收录（签名列表不含 G 系）；zh 套件未收录

## 语义

把源 Graphics（srcID）以掩码 Graphics（maskID）为掩码，绘制到目标 Graphics（ID）的 (destX, destY) 处。掩码绘制把掩码图像的蓝色（B 通道）值作为不透明度：掩码为纯白（蓝 = 255）处源图像原样绘制，为纯黑（蓝 = 0）处源图像完全透明不绘制，中间值为半透明混合。

处理成功返回 `1`。失败返回 `0` 的条件：目标、源或掩码任一未创建；srcID 与 maskID 的宽高不一致；绘制区域超出目标 Graphics 范围。该处理由 CPU 单线程逐像素完成，速度很慢。

G 系指令要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报错。

## 用法

### int GDRAWGWITHMASK(destID, srcID, maskID, destX, destY)
- destID：绘制目标 Graphics ID。
- srcID：绘制源 Graphics ID，须与 maskID 同宽高。
- maskID：掩码 Graphics ID，其蓝通道作为不透明度。
- destX、destY：绘制位置（目标内部坐标）。
- 返回值：成功 `1`；上述任一失败 `0`。
```erb
GCREATE 0, 100, 100          ; 目标
GCREATEFROMFILE 1, "pic.png"      ; 源
GCREATEFROMFILE 2, "mask.png"     ; 掩码（与源同尺寸）
GDRAWGWITHMASK 0, 1, 2, 0, 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:186`（`["GDRAWGWITHMASK"] = new GraphicsDrawGWithMaskMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6326`（`GraphicsDrawGWithMaskMethod`）；混合核心 `UI/Game/Image/GraphicsImage.cs:354`（`GDrawGWithMask`）

```text
GraphicsDrawGWithMaskMethod:
构造：返回类型 = long；参数 = [int, int, int, int, int]；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI: 抛 CodeEE（仅 GDI+ 绘制方式可用）
    dest = ReadGraphics(args[0]) ; 未创建 → 返回 0
    src  = ReadGraphics(args[1]) ; 未创建 → 返回 0
    mask = ReadGraphics(args[2]) ; 未创建 → 返回 0
    若 src.Width != mask.Width 或 src.Height != mask.Height: 返回 0
    destPoint = ReadPoint(args[3], args[4])   ; 越出 int 范围 → CodeEE
    若 destPoint.X + src.Width > dest.Width 或 destPoint.Y + src.Height > dest.Height:
        返回 0                                 ; 超出目标范围
    dest.GDrawGWithMask(src, mask, destPoint)
    返回 1

GraphicsImage.GDrawGWithMask(src, mask, destPoint):
    Load()；drawImgList = null
    srcBytes  = 源位图字节（BGRA）
    maskBytes = 掩码位图字节
    锁定目标位图（Format32bppArgb，可读写）
    for y in 0 .. src.Height-1:
        for x in 0 .. src.Width-1:
            b = maskBytes 的蓝通道字节（BGRA 首字节）
            若 b == 255: 目标 4 字节 ← 源 4 字节        ; 完全不透明，直接拷贝
            若 b == 0:   跳过 4 字节                    ; 完全透明，不动目标
            否则:
                m = b + 1                               ; (alpha+1)/256 近似
                目标 B/G/R/A 各字节 ← (源*m + 目标*(256-m)) >> 8
    解锁位图
```

## 备注

- 文档「把掩码图像的蓝色值作为不透明度」与源码一致：BGRA 字节序的首字节即 Blue，用它决定混合比例。
- 文档「掩码纯白原样绘制、纯黑不发生」对应代码中 255 / 0 的快速分支；中间值用 (alpha+1)/256 加权混合（源码注释承认与标准 alpha/255 有微小误差）。
- 文档「srcID 与 maskID 宽高完全一致、且绘制区域不超出 destID 才成功」与源码的返回 0 条件一致；负的 destX/destY 只要仍落在目标内即被允许（仅检查右/下边界）。
- 文档强调 CPU 单线程慢速——对应 LockBits + 逐像素循环的实现。
