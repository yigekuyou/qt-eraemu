# GGETFONTSTYLE

- **类别**：式中函数（G 系图像处理函数，EE 扩展；与 `GCREATED` 等共用 `GraphicsStateMethod`）
- **签名**：int GGETFONTSTYLE(int ID)
- **文档来源**：`ecd/Command.md` 图像处理章节以指令形式记载 G 系函数但**未收录本函数名**；`ecd/Expression.md` 未收录；zh 套件未收录。既有文档 `GSETFONT.md`/`GCREATED.md` 已提到本名字；语义据源码

## 语义

取得指定 ID 的 Graphics 当前字体的**样式位**。

- 返回值（`GraphicsImage.Fontstyle`，`UI/Game/Image/GraphicsImage.cs:612-628`）为按位组合：
  | 位值 | 含义 |
  |---|---|
  | `1` | 粗体 Bold |
  | `2` | 斜体 Italic |
  | `4` | 删除线 Strikeout |
  | `8` | 下划线 Underline |
  （`0` = Regular；可同时成立，如粗体+斜体 = 3。）
- Graphics 未创建/已废弃 → 返回 `0`。ID 为负数或超过 int 上限 → 抛 `CodeEE`；绘制方式为 `WINAPI` → 抛 `CodeEE`（仅 GDI+ 系可用）。
- 与 `GSETFONT` 配对：`GSETFONT(ID, 名称, 字号, 样式位)`（第 4 参数为 EE 扩展）写入的值可用本函数读回（既有文档 `test/data/functions/GSETFONT.md` 记录了这一对应关系）。
- 与 `GGETBRUSH`、`GGETPEN`、`GGETPENWIDTH`、`GGETFONTSIZE`、`GWIDTH`、`GHEIGHT`、`GCREATED` 共用实现类 `GraphicsStateMethod`（按注册名分支，`Runtime/Script/Statements/Function/Creator.Method.cs:5321-5342`）。

## 用法

### int GGETFONTSTYLE(ID)
- ID：Graphics 的 ID（0 以上整数）。
- 返回值：样式位（1/2/4/8 的组合）；未创建 `0`。
```erb
GCREATE 0, 200, 100
GSETFONT 0, "MS Gothic", 16, 3      ; 粗体 + 斜体（1 | 2）
PRINTFORML {GGETFONTSTYLE(0)}       ;→ 3
PRINTFORML {GGETBRUSH(0)}           ;→ 0（未设画刷）

GDISPOSE 0
PRINTFORML {GGETFONTSTYLE(0)}       ;→ 0（已废弃）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:320`（`["GGETFONTSTYLE"] = new GraphicsStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5305`（`GraphicsStateMethod`，`GGETFONTSTYLE` 分支 `:5332-5333`）
- 样式位来源：`UI/Game/Image/GraphicsImage.cs:612`（`Fontstyle` getter）
- 设置侧：`GraphicsSetFontMethod`（`Runtime/Script/Statements/Function/Creator.Method.cs:5447`）

```text
构造（Creator.Method.cs:5307-5312）:
    返回类型 = long；参数 = [Int]；CanRestructure = false
    （GCREATED/GWIDTH/GHEIGHT/GGETFONTSIZE/GGETFONTSTYLE/GGETPEN/GGETPENWIDTH/GGETBRUSH 共用）

GetIntValue(exm, args)（Creator.Method.cs:5313-5344）:
    if Config.TextDrawingMode == WINAPI: throw CodeEE(GDIPlusOnly, Name)
    g = ReadGraphics(Name, exm, args, 0)      ; args[0] → ID；负数/>int.MaxValue → CodeEE
    if !g.IsCreated: return 0
    switch Name:
        ...
        "GGETFONTSTYLE" → g.Fontstyle         ; ← 本函数
        ...

GraphicsImage.Fontstyle（GraphicsImage.cs:612-628）:
    ret = 0
    if 样式含 Bold:      ret |= 1
    if 样式含 Italic:    ret |= 2
    if 样式含 Strikeout: ret |= 4
    if 样式含 Underline: ret |= 8
    return ret
```

## 备注

- 与既有文档的关系：`GSETFONT.md` 已写明「所设置的字体可被 `GGETFONT`/`GGETFONTSIZE`/`GGETFONTSTYLE` 读取」，其中的位含义（1/2/4/8）与本条目的源码分支完全一致；`GCREATED.md` 也记载了本名字与实现类的对应。`ecd/Command.md` 未单独列出本名字。
- 返回 `0` 的两种情况：**未创建** 与 **Regular 样式**（既没有粗/斜/删/下划线）。要区分请先 `GCREATED(ID)`。
- 样式位与 `GSETFONT` 第 4 参数同编码，可直接中转（`GSETFONT 0, "字体", 16, GGETFONTSTYLE(0)`）。
- 本仓库移植版（`src/eraengine/`）未实现本函数。
