# SPRITECREATE

- **类别**：式中函数
- **签名**：
  - int SPRITECREATE(str spriteName, int gID)
  - int SPRITECREATE(str spriteName, int gID, int x, int y, int width, int height)
- **文档来源**：`ecd/docs/translation/Command.md`「SPRITECREATE `<精灵名>`, `<Graphics ID>`」与「SPRITECREATE `<精灵名>`, `<Graphics ID>`, `<x>`, `<y>`, `<宽度>`, `<高度>`」小节（ecd 把图像处理系列在 Command.md 中按指令形式记载；zh 套件未单独立节，仅在 Resource.md 中提到资源声明）

## 语义

以 gID 指定 Graphics 的一部分或全部为基础，创建名为 spriteName 的精灵（sprite）。带 6 参数的用法可指定切取范围 (x, y, width, height)。创建成功返回非 0（1），失败（如同名精灵已存在且已创建、父 Graphics 未创建、精灵名为空）返回 0。

精灵只记录父 Graphics 的 ID 与切取位置，因此父 Graphics 内容变化时精灵也随之变化；父 Graphics 被废弃时精灵也被视为已废弃。创建出的精灵可与 resources 文件夹 CSV 中声明的资源几乎同样使用（如 `PRINT_IMG`、`HTML_PRINT` 的 img 标签）。

注意：ecd 文档把它按指令形式记载在 Command.md 的图像处理组，但源码中它注册为式中函数（FunctionMethod），在表达式中求值并返回整数；本仓库源码以式中函数为准。

仅当绘制方式（TextDrawingMode）为 `GRAPHICS` 或 `TEXTRENDERER` 时可用；WINAPI 模式下调用会抛出运行时错误。

## 用法

### SPRITECREATE(spriteName, gID)
- spriteName：精灵资源名（字符串，将转为大写后登记）。为空时直接返回 0。
- gID：作为父图像的 Graphics 编号。
```erb
;先创建一个 100x100 的 Graphics，再以其整体创建精灵
GCREATE 0, 100, 100
IF SPRITECREATE("MYSPRITE", 0) == 0
	PRINTL 精灵创建失败
ENDIF
```
### SPRITECREATE(spriteName, gID, x, y, width, height)
- x, y, width, height：从父 Graphics 上切取的矩形范围。
- 矩形正负皆可（宽高为负时按绝对值作为输出尺寸），但必须与父图像范围 (0,0,g.Width,g.Height) 相交，否则抛出「图像引用越界」的 CodeEE。
```erb
GCREATE 0, 100, 100
SPRITECREATE("CUT", 0, 10, 10, 50, 50)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:192`（`["SPRITECREATE"] = new SpriteCreateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6062`（`sealed class SpriteCreateMethod : FunctionMethod`，`GetIntValue`）；辅助：同文件 `ReadGraphics`(:5159)、`ReadRectangle`(:5215)，以及 `UI/Game/Image/AppContents.cs:44`（GetSprite）、`:82`（CreateSpriteG）

```text
函数 SPRITECREATE(参数表):
    若 Config.TextDrawingMode == WINAPI:
        抛出 CodeEE（"仅 GDI+ 绘制模式可用"）
    imgname <- 参数[0] 的字符串值
    若 imgname 为空: 返回 0
    img <- AppContents.GetSprite(imgname)        # 名字转大写后查表
    若 img != null 且 img.IsCreated: 返回 0      # 同名已创建精灵已存在
    g <- ReadGraphics(参数[1]):                  # gID 为负或超过 int.MaxValue 抛 CodeEE
        若 gID < 0 或 gID > int.MaxValue: 抛出 CodeEE
        返回 AppContents.GetGraphics(gID)（不存在时新建空 GraphicsImage 并登记）
    若 !g.IsCreated: 返回 0                      # 父 Graphics 未创建
    rect <- (0, 0, g.Width, g.Height)
    若 参数个数为 6:
        rect <- ReadRectangle(参数[2..5])
            # x,y 须在 int 范围内；width、height 不得为 0，否则抛 CodeEE
        （EM_私家版修改）若 rect 与 (0,0,g.Width,g.Height) 不相交:
            抛出 CodeEE（"图像引用越界"）
        # 原版实现（已注释掉）要求 rect 完全落在父图像内，否则抛错
    AppContents.CreateSpriteG(imgname, g, rect)
        # imgname 转大写；new SpriteG(imgname, 父Graphics, rect) 并放入 imageDictionary
        # imgname 为空时抛 ArgumentOutOfRangeException（此处前置已拦截）
    返回 1
```

## 备注

- ecd Command.md 将本函数以指令形式（`### SPRITECREATE`）记载于图像处理组，并描述「处理成功时返回非 0」；源码中实为式中函数，成功返回 1。
- 文档称切取范围「不能指向父图像之外」；本仓库当前实现（EM_私家版_SPRITECREATE範囲制限緩和）已放宽为「与父图像相交即可」，原版更严格的越界检查代码被注释保留。
- 失败返回 0 的情形在源码中还包括：精灵名为空、父 Graphics 未创建；文档只举了「同名资源已存在」。
- zh 套件未收录本函数的独立小节，仅在 Resource.md 中描述了 CSV 资源声明形式的精灵。
