# GCREATE

- **类别**：式中函数（G 系图像处理指令，可作命令或函数使用）
- **签名**：int GCREATE(`<ID>`, `<宽度>`, `<高度>`)
- **文档来源**：`ecd/Command.md`「### GCREATE `<ID>`, `<宽度>`, `<高度>`」（图像处理相关章节）；`ecd/Expression.md` 未收录（签名列表不含 G 系）；zh 套件未收录

## 语义

以指定尺寸创建指定 ID 的 Graphics（32 位 ARGB 位图）。Graphics 的 ID 必须是 0 以上的整数，宽、高必须分别是 1 以上 8192（`AbstractImage.MAX_IMAGESIZE`）以下的整数，参数超出范围会报错（CodeEE）。

创建成功时返回 `1`（非 0）；指定 ID 的 Graphics 已创建时本函数什么都不做并返回 `0`。要重建 Graphics，须先用 `GDISPOSE` 废弃已有的 Graphics。

G 系指令要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报错。

## 用法

### int GCREATE(ID, width, height)
- ID：Graphics 的 ID，0 以上整数（负数或超过 int.MaxValue 报错）。
- width、height：尺寸，1～8192；≤0 或 >8192 报错。
- 返回值：创建成功 `1`；该 ID 已被占用（已创建）`0`。
```erb
IF GCREATE(0, 200, 100) == 0
	PRINTL ID0 已存在，先 GDISPOSE
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:178`（`["GCREATE"] = new GraphicsCreateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5937`（`GraphicsCreateMethod`）；创建核心 `UI/Game/Image/GraphicsImage.cs:79`（`GCreate`）

```text
GraphicsCreateMethod:
构造：返回类型 = long；参数 = [int, int, int]；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛 CodeEE（本函数仅在 GDI+ 绘制方式下可用）
    g = ReadGraphics(Name, exm, args, 0)
        ; args[0] → ID；负数或 > int.MaxValue → CodeEE
    若 g.IsCreated: 返回 0           ; 已创建，不做任何事
    p = ReadPoint(Name, exm, args, 1)  ; args[1]、args[2] → (width, height)，越 int 范围报错
    width = p.X; height = p.Y
    若 width <= 0:  抛 CodeEE（Width 为 0 以下的值）
    若 width > AbstractImage.MAX_IMAGESIZE (=8192): 抛 CodeEE（Width 过大）
    若 height <= 0: 抛 CodeEE（Height 为 0 以下的值）
    若 height > 8192: 抛 CodeEE（Height 过大）
    g.GCreate(width, height, false)
        ; GDispose() 释放旧资源 → new Bitmap(w, h, Format32bppArgb)
        ; size = (w, h)；g = Graphics.FromImage(RealBitmap)；drawImgList = []
        ; 并把自身登记进 AppContents.tempLoadedGraphicsImages
    返回 1
```

## 备注

- 文档说「创建成功时返回非 0」——源码固定返回 `1`；「已创建时返回 0」一致。
- 文档的尺寸上限「8192」与源码常量 `AbstractImage.MAX_IMAGESIZE = 8192`（`UI/Game/Image/AImage.cs:8`）一致。
- 文档未提创建的是 32 位 ARGB 位图（支持透明），这是 `GCreate` 的实现细节。
