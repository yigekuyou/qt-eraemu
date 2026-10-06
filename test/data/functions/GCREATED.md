# GCREATED

- **类别**：式中函数（G 系图像处理函数）
- **签名**：int GCREATED(`<ID>`)
- **文档来源**：`ecd/Command.md`「### GCREATED `<ID>`」（图像处理相关章节）；`ecd/Expression.md` 未收录（签名列表不含 G 系）；zh 套件未收录

## 语义

查询指定 ID 的 Graphics 是否已创建：已创建返回 `1`，未创建（包括已被 `GDISPOSE` 废弃）返回 `0`。不创建、不修改任何图像，是纯查询函数。

G 系函数要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报错。ID 为负数或超过 int 上限时同样报错（在读取 ID 的公共检查中抛 CodeEE）。

本函数与 `GWIDTH`、`GHEIGHT`、`GGETFONTSIZE`、`GGETFONTSTYLE`、`GGETPEN`、`GGETPENWIDTH`、`GGETBRUSH` 共用同一个实现类（按注册名分支）。

## 用法

### int GCREATED(ID)
- ID：Graphics 的 ID，0 以上整数。
- 返回值：已创建 `1`；未创建/已废弃 `0`。
```erb
IF GCREATED(0) == 0
	GCREATE 0, 200, 100
ENDIF
GCLEAR 0, 0xFF000000
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:172`（`["GCREATED"] = new GraphicsStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5305`（`GraphicsStateMethod`）

```text
GraphicsStateMethod:
构造：返回类型 = long；参数 = [int]；CanRestructure = false。
（该类同时服务 GCREATED/GWIDTH/GHEIGHT/GGETFONTSIZE/GGETFONTSTYLE/
  GGETPEN/GGETPENWIDTH/GGETBRUSH，按 Name 分支。）

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛 CodeEE（本函数仅在 GDI+ 绘制方式下可用）
    g = ReadGraphics(Name, exm, args, 0)
        ; args[0] → ID；负数或 > int.MaxValue → CodeEE
    若 !g.IsCreated: 返回 0
    switch (Name):
        "GCREATED"      → 返回 1
        "GWIDTH"        → 返回 g.Width
        "GHEIGHT"       → 返回 g.Height
        "GGETFONTSIZE"  → 返回 g.Fontsize
        "GGETFONTSTYLE" → 返回 g.Fontstyle
        "GGETPEN"       → 返回 g.Pen.Color.ToArgb() & 0xFFFFFFFF
        "GGETPENWIDTH"  → 返回 (long)g.Pen.Width
        "GGETBRUSH"     → 返回 ((SolidBrush)g.Brush).Color.ToArgb() & 0xFFFFFFFF
    其他 → 抛 ExeEE（内部异常分支）
```

## 备注

- 文档「指定 ID 的 Graphics 已创建时获取 1，未创建（包括已废弃）时获取 0」与源码完全一致。
- ecd 文档将本函数放在「图像处理相关」的指令章节中说明，实际它是纯查询型式中函数，无命令/函数双形态问题（返回值在表达式里直接取用）。
- ID 不合法（负数等）时报错而非返回 0；只有「ID 合法但未创建」才返回 0。
