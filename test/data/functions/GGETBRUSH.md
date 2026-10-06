# GGETBRUSH

- **类别**：式中函数（G 系图像处理函数，EE 扩展；与 `GCREATED` 等共用 `GraphicsStateMethod`）
- **签名**：int GGETBRUSH(int ID)
- **文档来源**：`ecd/Command.md` 图像处理章节以指令形式记载了 G 系函数，但**未收录本函数名**；`ecd/Expression.md` 未收录；zh 套件未收录。语义据源码与同族既有文档（`GCREATED.md`/`GSETBRUSH` 等）

## 语义

取得指定 ID 的 Graphics 当前使用的**画刷颜色**（ARGB 整数）。

- 返回值：`(SolidBrush)Brush.Color.ToArgb() & 0xFFFFFFFF`，即 32 位 ARGB（A<<24 | R<<16 | G<<8 | B），可用 `GSETBRUSH` 设置回去或与颜色常量比较。
- Graphics 未创建/已废弃 → 返回 `0`（不是报错）。ID 为负数或超过 int 上限 → 抛 `CodeEE`；绘制方式为 `WINAPI` → 抛 `CodeEE`（仅 GDI+ 系可用）。
- 与 `GSETBRUSH(ID, 颜色)` 成对：本函数读、`GSETBRUSH` 写；`GSETBRUSH` 写入的总是 `SolidBrush`（`Runtime/Script/Statements/Function/Creator.Method.cs:5441`），故本函数的 `(SolidBrush)` 强转在实际路径上安全（推定）。
- 与 `GGETFONTSTYLE`、`GGETPEN`、`GGETPENWIDTH`、`GGETFONTSIZE`、`GWIDTH`、`GHEIGHT`、`GCREATED` 共用同一实现类 `GraphicsStateMethod`，按注册名 `Name` 分支（`Runtime/Script/Statements/Function/Creator.Method.cs:5321-5342`）。

## 用法

### int GGETBRUSH(ID)
- ID：Graphics 的 ID（0 以上整数，`GCREATE` 生成的）。
- 返回值：画刷的 ARGB 值；未创建 `0`。
```erb
GCREATE 0, 200, 100
GSETBRUSH 0, 0xFF3366CC          ; 设置画刷颜色
PRINTFORML {GGETBRUSH(0)}        ;→ 4281167564（0xFF3366CC 的十进制）
PRINTFORML {TOSTR(0xFF3366CC)}   ; 同值对照
PRINTFORML {GDISPOSE(0)}
PRINTFORML {GGETBRUSH(0)}        ;→ 0（已废弃）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:322`（`["GGETBRUSH"] = new GraphicsStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5305`（`GraphicsStateMethod`，`GGETBRUSH` 分支 `:5338-5340`）
- 设置侧对照：`GraphicsSetBrushMethod`（`Runtime/Script/Statements/Function/Creator.Method.cs:5424`，注册 `Runtime/Script/Statements/Function/Creator.cs:188`）
- 画刷字段：`UI/Game/Image/GraphicsImage.cs:631`（`Brush`）

```text
构造（Creator.Method.cs:5307-5312）:
    返回类型 = long；参数 = [Int]；CanRestructure = false
    （GCREATED/GWIDTH/GHEIGHT/GGETFONTSIZE/GGETFONTSTYLE/GGETPEN/GGETPENWIDTH/GGETBRUSH 共用）

GetIntValue(exm, args)（Creator.Method.cs:5313-5344）:
    if Config.TextDrawingMode == WINAPI: throw CodeEE(GDIPlusOnly, Name)
    g = ReadGraphics(Name, exm, args, 0)          ; args[0] → ID；负数/>int.MaxValue → CodeEE
    if !g.IsCreated: return 0
    switch Name:
        "GCREATED"      → 1
        "GWIDTH"        → g.Width
        "GHEIGHT"       → g.Height
        "GGETFONTSIZE"  → g.Fontsize
        "GGETFONTSTYLE" → g.Fontstyle
        "GGETPEN"       → g.Pen.Color.ToArgb() & 0xffffffffL
        "GGETPENWIDTH"  → (long)g.Pen.Width
        "GGETBRUSH"     → ((SolidBrush)g.Brush).Color.ToArgb() & 0xffffffffL   ; ← 本函数
    其他 → throw ExeEE（内部异常）
```

## 备注

- 与既有文档的关系：`test/data/functions/GCREATED.md` 已说明该类同时服务 `GGETBRUSH` 等 8 个名字，但当时 `GGETBRUSH` 自身尚无条目（本批补上）；`ecd/Command.md` 的图像处理章节也未列本名字，故本条目为纯源码语义。
- 返回 `0` 的两种情况要区分：**未创建**（合法 ID 但没 `GCREATE`）与「画刷颜色恰为 0x00000000」（全透明黑）。要区分可先用 `GCREATED`。
- ARGB 的位序与 `GSETBRUSH`/`GSETCOLOR` 接受的颜色常量一致（`0xAARRGGBB`），可直接互相传递。
- 本仓库移植版（`src/eraengine/`）未实现本函数。
