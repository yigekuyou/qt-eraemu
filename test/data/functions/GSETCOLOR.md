# GSETCOLOR

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（G 系图像处理函数）
- **签名**：int GSETCOLOR(`<ID>`, `<颜色>`, `<x>`, `<y>`)
- **文档来源**：`ecd/Command.md`「### GSETCOLOR `<ID>`, `<颜色>`, `<x>`, `<y>`」（图像处理相关章节，按指令形式记载）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

把指定 ID 的 Graphics 中 (x, y) 位置的像素替换为指定颜色。处理成功返回 1（非 0）；Graphics 未创建/已废弃，或 (x, y) 超出图像范围时返回 0，不产生副作用。

G 系函数要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报运行时错误。ID 为负数或超过 int 上限、颜色值不在 0～0xFFFFFFFF 范围、x/y 超出 int 范围时抛 CodeEE。

文档特别提醒：该指令速度很慢，与 `GGETCOLOR` 配合逐像素改写整张大图无法在实用时间内完成。

## 用法

### int GSETCOLOR(ID, 颜色, x, y)
- ID：Graphics 的 ID，0 以上整数。
- 颜色：`0xAARRGGBB` 形式的整数值（含 alpha 通道）。
- x、y：像素坐标，0 基；超出图像范围时返回 0。
- 返回值：成功 1；失败 0。
```erb
GCREATE 0, 32, 32
GSETCOLOR 0, 0xFFFF0000, 5, 5   ; 把 (5,5) 涂成不透明红
PRINTV GSETCOLOR(0, 0xFF00FF00, 5, 5)  ; 再改一次，输出 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:184`（`["GSETCOLOR"] = new GraphicsSetColorMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5399`（`GraphicsSetColorMethod`）

```text
GraphicsSetColorMethod:
构造：返回类型 = long；参数 = [long, long, long, long]；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛 CodeEE（仅 GDI+ 绘制方式可用）
    g = ReadGraphics(Name, exm, args, 0)
        ; args[0] → ID；负数或 > int.MaxValue → CodeEE
    若 !g.IsCreated: 返回 0
    c = ReadColor(Name, exm, args, 1)
        ; args[1] → 颜色；< 0 或 > 0xFFFFFFFF → CodeEE
        ; 拆成 A/R/G/B 四字节构造 Color
    p = ReadPoint(Name, exm, args, 2)
        ; args[2]、args[3] → x、y；超出 int 范围 → CodeEE
    若 p.X < 0 || p.X >= g.Width || p.Y < 0 || p.Y >= g.Height:
        返回 0
    g.GSetColor(c, p.X, p.Y)   ; 把该像素设为颜色 c
    返回 1
```

## 备注

- ecd/Command.md 按「指令」形式记载 G 系，但本仓库中 GSETCOLOR 是注册在 `Runtime/Script/Statements/Function/Creator.cs` 的式中函数（在表达式内调用、有返回值），无同名命令。
- 文档只说「成功时返回非 0」，源码明确成功返回 1、失败返回 0，语义一致。
- 源码的越界判断写作 `p.X < 0 || p.X >= g.Width || p.X < 0 || p.Y >= g.Height`（`p.X < 0` 重复出现，且缺少对 `p.Y < 0` 的显式判断；`p.Y < 0` 时会落到 `GSetColor` 由底层处理）。此为源码层面的微小笔误，不影响正常坐标范围下的行为。
