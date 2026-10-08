# GDASHSTYLE

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（本仓库中实现为式中函数）
- **签名**：
  - `GDASHSTYLE <GraphicsID>, <DashStyle>, <DashCap>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・GDASHSTYLE gID, DashStyle, DashCap」。ecd 套件与 zh 套件均未收录本命令。

## 语义

为指定 Graphics 设置 `GDRAWLINE` 画线时的线型（虚线样式）与线端形状。两个参数分别为 C# `System.Drawing.Drawing2D` 中 `DashStyle`、`DashCap` 枚举的数值：

- DashStyle：`0` 普通实线、`1` 虚线（dash）、`2` 点线（dot）、`3` 点划线（dash-dot）、`4` 双点划线（dash-dot-dot）。
- DashCap（线端形状）：`0` 平角（直角）、`2` 圆角、`3` 三角形；`1` 为欠号。

绘制方式为 `WINAPI` 时不能使用（GDI+ 专用），会报错；指定 ID 的 Graphics 未创建时返回 0。本命令只改笔的属性，不进行绘制、无可见副作用，实际画线仍由 `GDRAWLINE` 完成（颜色与粗细来自 `GSETPEN`）。

## 用法

### `GDASHSTYLE <GraphicsID>, <DashStyle>, <DashCap>`
```erb
GCREATE 0, 300, 100
GSETPEN 0, 0xFF0000FF, 3     ;蓝色、粗 3
GDASHSTYLE 0, 2, 2           ;点线、圆角线端
GDRAWLINE 0, 10, 50, 290, 50
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:333`（`["GDASHSTYLE"] = new GraphicsSetDashStyleMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5544`（`GraphicsSetDashStyleMethod`，`#region EE_GDASHSTYLE`）；底层 `UI/Game/Image/GraphicsImage.cs:467`（`GDashStyle(long, long)`）

```text
构造: ReturnType = 整数; argumentTypeArray = [long, long, long]; CanRestructure = false

GetIntValue(exm, arguments):
    if Config.TextDrawingMode == WINAPI:
        throw CodeEE(「该命令只能在 GDI+（GRAPHICS/TEXTRENDERER）绘制方式下使用」)
    g = ReadGraphics(Name, exm, arguments, 0)     # 解析第1参数为 Graphics ID
    if !g.IsCreated: return 0
    g.GDashStyle(arguments[1].GetIntValue(exm), arguments[2].GetIntValue(exm))
    return 1

# GraphicsImage.GDashStyle
GDashStyle(style, cap):
    if g == null: throw NullReferenceException
    if pen == null: pen = new Pen(Config.ForeColor)    # 未 GSETPEN 时先造默认笔
    pen.DashStyle = (DashStyle)style                   # 直接强转枚举，无取值校验
    pen.DashCap   = (DashCap)cap
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准，两者一致。
- readme 特注「DashCap 的 1 是欠号，有意见请向 Microsoft 提」——即 `DashCap` 枚举没有取值 1。
- 参数值不做范围校验，直接强转为枚举：传入非法数值（如 DashCap=1）时 .NET 会抛异常，而非返回 0。
