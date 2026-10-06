# GWIDTH

- **类别**：式中函数（G 系图像处理函数）
- **签名**：int GWIDTH(`<ID>`)
- **文档来源**：`ecd/Command.md`「### GWIDTH `<ID>`」（图像处理相关章节，按指令形式记载）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

获取指定 ID 的 Graphics 的宽度（像素）。未创建（包括已被 `GDISPOSE` 废弃）时返回 0。是纯查询函数，不修改任何图像。

G 系函数要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报运行时错误。ID 为负数或超过 int 上限时抛 CodeEE（读取 ID 的公共检查）。

本函数与 `GCREATED`、`GHEIGHT` 等共用同一个实现类（按注册名分支）。

## 用法

### int GWIDTH(ID)
- ID：Graphics 的 ID，0 以上整数。
- 返回值：该 Graphics 的宽度；未创建/已废弃时 0。
```erb
GCREATE 0, 300, 200
PRINTV GWIDTH(0)   ; 输出 300
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:173`（`["GWIDTH"] = new GraphicsStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5305`（`GraphicsStateMethod`，与 GCREATED/GHEIGHT 等共用）

```text
GraphicsStateMethod:
构造：返回类型 = long；参数 = [long]；CanRestructure = false。
（该类同时服务 GCREATED/GWIDTH/GHEIGHT/GGETFONTSIZE/GGETFONTSTYLE/
  GGETPEN/GGETPENWIDTH/GGETBRUSH，按 Name 分支。）

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛 CodeEE（仅 GDI+ 绘制方式可用）
    g = ReadGraphics(Name, exm, args, 0)
        ; args[0] → ID；负数或 > int.MaxValue → CodeEE
    若 !g.IsCreated: 返回 0
    switch (Name):
        "GWIDTH" → 返回 g.Width
        （其他名字走各自分支；GWIDTH 只取 g.Width）
    不该到达的分支 → 抛 ExeEE
```

## 备注

- ecd/Command.md 把 G 系按「指令」形式记载，但本仓库中 GWIDTH 是注册在 `Runtime/Script/Statements/Function/Creator.cs` 的式中函数（在表达式内调用、有返回值），无同名命令。
- 文档称「未创建时返回 0」，与源码一致（`!g.IsCreated → 0`）。
- 源码中 GraphicsStateMethod 还服务 EE 扩展的 GGETFONTSIZE/GGETFONTSTYLE/GGETPEN/GGETPENWIDTH/GGETBRUSH 分支，与本函数共用同一实现。
