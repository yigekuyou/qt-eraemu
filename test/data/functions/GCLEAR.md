# GCLEAR

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（G 系图像处理指令，可作命令或函数使用）
- **签名**：int GCLEAR(`<ID>`, `<颜色>`)
- **文档来源**：`ecd/Command.md`「### GCLEAR `<ID>`, `<颜色>`」（图像处理相关章节）；`ecd/Expression.md` 未收录（签名列表不含 G 系）；zh 套件未收录

## 语义

用指定颜色填充（替换）指定 ID 的 Graphics 的全部区域，成功返回 `1`，指定 ID 的 Graphics 未创建（含已废弃）时返回 `0`。

颜色不是 RGB 而是 ARGB 形式（`0xAARRGGBB`），范围 `0`～`0xFFFFFFFF`，越界会报错（CodeEE）。G 系指令要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；绘制方式为 `WINAPI` 时报错。

本仓库为「私家版 GCLEAR 扩展」（EM_私家版_GCLEAR拡張）额外支持 6 参数形式 `GCLEAR(ID, 颜色, x, y, 宽, 高)`，只清除指定矩形区域——这是文档没有记载的行为。

## 用法

### int GCLEAR(ID, color)
- ID：目标 Graphics 的 ID，负数或超过 int 上限时报错。
- color：ARGB 颜色（`0xAARRGGBB`），如 `0xFF000000` 为不透明黑。
- 返回值：成功 `1`，Graphics 未创建 `0`。
```erb
GCREATE 0, 200, 100
GCLEAR 0, 0xFF000000       ; 整块涂黑
```
### int GCLEAR(ID, color, x, y, width, height)（EE 扩展，文档未收录）
- x、y、width、height：要清除的矩形区域。
- 实现为对 Graphics 设置裁剪区域后 Clear，其余区域保持不变。
```erb
GCLEAR 0, 0x00000000, 0, 0, 100, 50   ; 只清上半部分为透明
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:181`（`["GCLEAR"] = new GraphicsClearMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6172`（`GraphicsClearMethod`，标注 `#region EM_私家版_GCLEAR拡張`）；填充核心 `UI/Game/Image/GraphicsImage.cs:106`（`GClear`）、`:114`（带矩形重载）

```text
GraphicsClearMethod:
构造：返回类型 = long；
    参数表支持 2 参 [int, int] 与 6 参 [int, int, int, int, int, int] 两种；
    CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛 CodeEE（本函数仅在 GDI+ 绘制方式下可用）
    g = ReadGraphics(Name, exm, args, 0)
        ; args[0] → ID；负数或 > int.MaxValue → CodeEE
        ; AppContents.GetGraphics(ID)
    c = ReadColor(Name, exm, args, 1)
        ; args[1] → ARGB；不在 [0, 0xFFFFFFFF] → CodeEE（颜色值不合适）
        ; 拆成 A、R、G、B 四字节构造 Color
    若 !g.IsCreated: 返回 0
    若 args.Count == 2:
        g.GClear(c)                       ; g.Clear(c)，整块清除
    否则:
        g.GClear(c, args[2], args[3], args[4], args[5])
        ; GClear(c, x, y, w, h)：Load(); g.SetClip(矩形); g.Clear(c); g.ResetClip()
        ; drawImgList = null
    返回 1
```

## 备注

- 文档仅记载 2 参数形式；源码（EE 私家版扩展）另支持 6 参数的矩形区域清除，文档未收录，已如实补记。
- 文档说「ID 或颜色指定不合适时会出错」：与源码一致——ID 越界、颜色超出 0～0xFFFFFFFF 时抛 CodeEE；但 Graphics 未创建时是返回 `0` 而不是报错。
- 文档描述「用指定颜色替换全部区域」对应 `Graphics.Clear`，透明色（A=0）也可用于清空。
