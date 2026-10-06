# CBGSETG

- **类别**：式中函数（CBG 系图像处理指令，可作命令或函数使用）
- **签名**：int CBGSETG(int Graphics ID, int x, int y, int z深度)
- **文档来源**：`ecd/Command.md`「### CBGSETG `<Graphics ID>`, `<x>`, `<y>`, `<z深度>`」（图像处理相关章节）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

把 `Graphics ID` 指定的 Graphics 设置为显示在客户端区域的背景图像。仅当绘制方式为 `GRAPHICS` 或 `TEXTRENDERER` 时可用，绘制方式为 `WINAPI` 时出错。

- `(x, y)` 指定 0 时，客户端区域的左下角与图像的左下角对齐显示。
- x 以右方向为正，y 以下方向为正，zdepth 以画面纵深方向为正。
- zdepth 必须指定 0 以外的值：普通文字绘制相当于 `zdepth == 0`；zdepth 为负时绘制在文字之前（更靠前）。zdepth 为 0 或超出 int 范围时抛出 CodeEE。
- 设置后内部列表按 zdepth 降序排序（zdepth 大者先绘制、看起来最靠里）。

返回值：设置成功返回 `1`；指定的 Graphics 未创建或已废弃时返回 `0`。

## 用法

### int CBGSETG(int Graphics ID, int x, int y, int z深度)
- `Graphics ID`：已创建的 Graphics 的 ID（0 以上整数）。为负或过大时抛出 CodeEE。
- `x`、`y`：显示位置偏移（int 范围，超出抛出 CodeEE）。
- `z深度`：绘制深度，非 0 的 int 值；为 0 或超出 int 范围时抛出 CodeEE。
- 返回值：成功 `1`，失败（Graphics 未创建等）`0`。
```erb
GCREATE 0, 640, 480
; ……向 Graphics 0 绘制背景……
; 显示在客户端区域左下对齐位置，深度 1（比文字靠里）
IF CBGSETG(0, 0, 0, 1)
	PRINTL 背景设置成功
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:195`（`["CBGSETG"] = new CBGSetGraphicsMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6634`（`CBGSetGraphicsMethod`）
- 辅助：`Runtime/Script/Statements/Function/Creator.Method.cs:5159`（`ReadGraphics`）、`Runtime/Script/Statements/Function/Creator.Method.cs:5199`（`ReadPoint`）
- 控制台侧：`UI/Game/EmueraConsole.cs:201`（`CBG_SetGraphics`）→ `:207`（`CBG_SetImage`）

```text
CBGSetGraphicsMethod:
构造：返回类型 = long；参数 = [long, long, long, long]；CanRestructure = false。
GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛出 CodeEE（只能在 GDI+ 绘制方式下使用）
    g ← ReadGraphics(Name, exm, args, 0)
        ; ID 为负 → CodeEE；ID > int.MaxValue → CodeEE
    若 !g.IsCreated 或 g.Bitmap == null: 返回 0
    p ← ReadPoint(Name, exm, args, 1)
        ; x、y 超出 int 范围 → CodeEE
    z64 ← args[3].GetIntValue(exm)
    若 z64 超出 int 范围 或 z64 == 0:
        抛出 CodeEE（第 4 参数超出范围，且不能为 0）
    exm.Console.CBG_SetGraphics(g, p.X, p.Y, (int)z64)
    返回 1

EmueraConsole.CBG_SetGraphics(gra, x, y, zdepth):
    若 gra == null 或 !gra.IsCreated: 返回 false
    返回 CBG_SetImage(new SpriteG("", gra, 全区域矩形), x, y, zdepth)
         ; 把 Graphics 包成无名精灵（Name = ""）后走统一入口

EmueraConsole.CBG_SetImage(image, x, y, zdepth):
    若 image == null 或 !image.IsCreated: 返回 false
    若 zdepth == 0: 抛出 ArgumentOutOfRangeException
    cbgList.Add(new ClientBackGroundImage(zdepth){ Img, x, y })
    cbgList.Sort()     ; 按 zdepth 降序（CompareTo 取负）
    返回 true
```

## 备注

- 文档明确「zdepth 指定 0 以外的值」，与源码一致（函数层抛 CodeEE，控制台层再抛 ArgumentOutOfRangeException 兜底）。
- `CBGSETG` 包成的无名精灵（Name 为空串）在 `CBGCLEAR`/`CBGCLEARBUTTON`/`CBGREMOVERANGE` 删除时会被 Dispose。
