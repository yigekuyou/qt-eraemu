# GSETBRUSH

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（G 系图像处理函数）
- **签名**：int GSETBRUSH(`<ID>`, `<颜色>`)
- **文档来源**：`ecd/Command.md`「### GSETBRUSH `<ID>`, `<颜色>`」（图像处理相关章节，按指令形式记载）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

为指定 ID 的 Graphics 设置指定颜色的画刷（SolidBrush）。设置的画刷被记忆在该 Graphics 中，直到用 `GDISPOSE` 废弃该 Graphics 或再次设置为止。处理成功返回 1；Graphics 未创建/已废弃时返回 0。

之后 `GFILLRECTANGLE` 指令填充矩形时使用的就是这里设置的画刷。

G 系函数要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报运行时错误。ID 为负数或超过 int 上限、颜色值不在 0～0xFFFFFFFF 范围时抛 CodeEE。

## 用法

### int GSETBRUSH(ID, 颜色)
- ID：Graphics 的 ID，0 以上整数。
- 颜色：`0xAARRGGBB` 形式的整数值（含 alpha 通道）。
- 返回值：成功 1；Graphics 未创建/已废弃 0。
```erb
GCREATE 0, 100, 100
GSETBRUSH 0, 0xFF0000FF          ; 蓝色画刷
GFILLRECTANGLE 0, 10, 10, 50, 30 ; 用该画刷填充矩形
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:188`（`["GSETBRUSH"] = new GraphicsSetBrushMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5424`（`GraphicsSetBrushMethod`）

```text
GraphicsSetBrushMethod:
构造：返回类型 = long；参数 = [long, long]；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛 CodeEE（仅 GDI+ 绘制方式可用）
    g = ReadGraphics(Name, exm, args, 0)
        ; args[0] → ID；负数或 > int.MaxValue → CodeEE
    若 !g.IsCreated: 返回 0
    c = ReadColor(Name, exm, args, 1)
        ; args[1] → 颜色；< 0 或 > 0xFFFFFFFF → CodeEE
        ; 拆成 A/R/G/B 四字节构造 Color
    g.GSetBrush(new SolidBrush(c))
        ; 旧画刷被 Dispose，新 SolidBrush 保存到 g.brush
    返回 1
```

## 备注

- ecd/Command.md 按「指令」形式记载 G 系，但本仓库中 GSETBRUSH 是注册在 `Runtime/Script/Statements/Function/Creator.cs` 的式中函数（在表达式内调用、有返回值），无同名命令。
- 文档只说「成功时返回非 0」，源码明确成功返回 1、失败返回 0，语义一致。
- 源码中 EE 扩展的 `GGETBRUSH` 函数可读回这里设置的画刷颜色。
