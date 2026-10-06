# GSETPEN

- **类别**：式中函数（G 系图像处理函数）
- **签名**：int GSETPEN(`<ID>`, `<颜色>`, `<笔宽>`)
- **文档来源**：`ecd/Command.md`「### GSETPEN `<ID>`, `<颜色>`, `<笔宽>`」（图像处理相关章节，按指令形式记载）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

为指定 ID 的 Graphics 设置指定颜色和宽度的画笔（Pen）。设置的画笔被记忆在该 Graphics 中，直到用 `GDISPOSE` 废弃该 Graphics 或再次设置为止。处理成功返回 1；Graphics 未创建/已废弃时返回 0。

文档（基于 1.824 版）称「截至 1.824 版，尚不存在使用所设置画笔的指令或函数」；本仓库已跟进 EE 扩展，画笔可被 `GGETPEN`/`GGETPENWIDTH` 读取，并被 `GDRAWLINE` 等绘图扩展使用。

G 系函数要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报运行时错误。

## 用法

### int GSETPEN(ID, 颜色, 笔宽)
- ID：Graphics 的 ID，0 以上整数。
- 颜色：`0xAARRGGBB` 形式的整数值（含 alpha 通道）。
- 笔宽：画笔宽度（整数）。
- 返回值：成功 1；Graphics 未创建/已废弃 0。
```erb
GCREATE 0, 100, 100
GSETPEN 0, 0xFFFF0000, 2   ; 红色、宽度 2 的画笔
; 之后由 EE 扩展的画线类函数使用该画笔
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:190`（`["GSETPEN"] = new GraphicsSetPenMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5518`（`GraphicsSetPenMethod`）

```text
GraphicsSetPenMethod:
构造：返回类型 = long；参数 = [long, long, long]；CanRestructure = false。
（源码注释：私家版旧代码漏了第 3 参数，现为三参数。）

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛 CodeEE（仅 GDI+ 绘制方式可用）
    g = ReadGraphics(Name, exm, args, 0)
        ; args[0] → ID；负数或 > int.MaxValue → CodeEE
    若 !g.IsCreated: 返回 0
    c = ReadColor(Name, exm, args, 1)
        ; args[1] → 颜色；< 0 或 > 0xFFFFFFFF → CodeEE
    width = args[2] 的整数值（不设范围检查）
    g.GSetPen(new Pen(c, width))
        ; GraphicsImage.GSetPen：保留旧画笔的 DashStyle/DashCap，
        ; Dispose 旧画笔，换上新画笔并恢复其线型/线帽
    返回 1
```

## 备注

- ecd/Command.md 按「指令」形式记载 G 系，但本仓库中 GSETPEN 是注册在 `Runtime/Script/Statements/Function/Creator.cs` 的式中函数（在表达式内调用、有返回值），无同名命令。
- 文档称「尚不存在使用所设置画笔的指令或函数」（1.824 版状态）；本仓库已有 `GGETPEN`/`GGETPENWIDTH`（读回颜色/宽度）及 EE 扩展绘图函数使用该画笔，该描述在本仓库已过时。
- 文档只说「成功时返回非 0」，源码明确成功返回 1、失败返回 0，语义一致。
