# GDRAWLINE

- **类别**：EE 扩展命令（本仓库中实现为式中函数）
- **签名**：
  - `GDRAWLINE <GraphicsID>, <起点X>, <起点Y>, <终点X>, <终点Y>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・GDRAWLINE gID, fromX, fromY, forX, forY」。ecd 套件与 zh 套件均未收录本命令。

## 语义

在指定 Graphics 上，从 `(fromX, fromY)` 到 `(forX, forY)` 画一条直线。线的颜色和粗细使用 `GSETPEN` 预先设置的笔；未用 `GSETPEN` 设置过笔时使用配置中的字体颜色（前景色）。readme 注明「式中関数としても使える」——本仓库即以式中函数实现，返回值兼作命令结果。

绘制方式为 `WINAPI` 时不能使用（GDI+ 专用），会报错；指定 ID 的 Graphics 未创建时返回 0，成功返回 1。

## 用法

### `GDRAWLINE <GraphicsID>, <起点X>, <起点Y>, <终点X>, <终点Y>`
```erb
GCREATE 0, 300, 200
GSETPEN 0, 0xFFFF0000, 2     ;红色、粗 2（颜色为 ARGB）
GDRAWLINE 0, 10, 10, 290, 190
;配合 GDASHSTYLE 可画虚线
GDASHSTYLE 0, 1, 0           ;虚线、平角线端
GDRAWLINE 0, 10, 100, 290, 100
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:331`（`["GDRAWLINE"] = new GraphicsDrawLineMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5807`（`GraphicsDrawLineMethod`，`#region EE_GDRAWLINE`）；底层 `UI/Game/Image/GraphicsImage.cs:449`（`GDrawLine`）

```text
构造: ReturnType = 整数; argumentTypeArray = [long, long, long, long, long]
      CanRestructure = false

GetIntValue(exm, arguments):
    if Config.TextDrawingMode == WINAPI:
        throw CodeEE(「该命令只能在 GDI+（GRAPHICS/TEXTRENDERER）绘制方式下使用」)
    g = ReadGraphics(Name, exm, arguments, 0)     # 第1参数 → Graphics
    if !g.IsCreated: return 0
    fromP = ReadPoint(Name, exm, arguments, 1)    # 第2、3参数 → 起点 (x, y)
    forP  = ReadPoint(Name, exm, arguments, 3)    # 第4、5参数 → 终点 (x, y)
    g.GDrawLine(fromP.X, fromP.Y, forP.X, forP.Y)
    return 1

# GraphicsImage.GDrawLine
GDrawLine(fromX, fromY, forX, forY):
    if g == null: throw NullReferenceException
    if pen != null:
        g.DrawLine(pen, fromX, fromY, forX, forY)          # 用 GSETPEN 设置的笔
    else:
        using (Pen p = new Pen(Config.ForeColor))          # 无笔时用配置前景色
            g.DrawLine(p, fromX, fromY, forX, forY)
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准，两者一致。
- readme「線の色と太さはGSETPENで指定したものを使用」与源码的 pen 优先逻辑一致；未设置笔时的回退（配置前景色、默认粗细 1）是 readme 未提及的实现细节。
