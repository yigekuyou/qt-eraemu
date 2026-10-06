# CBGSETBMAPG

- **类别**：式中函数（CBG 系图像处理指令，可作命令或函数使用）
- **签名**：int CBGSETBMAPG(int Graphics ID)
- **文档来源**：`ecd/Command.md`「### CBGSETBMAPG `<Graphics ID>`」（图像处理相关章节）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

把 `Graphics ID` 指定的 Graphics 设置为客户端区域的按钮映射。仅当绘制方式为 `GRAPHICS` 或 `TEXTRENDERER` 时可用，绘制方式为 `WINAPI` 时出错。

- 按钮映射影响 `CBGSETBUTTONSPRITE` 指令和 `INPUTMOUSEKEY` 指令：鼠标光标正下方的按钮映射图像颜色会被识别为按钮值。
- 按钮映射图像不会被显示；其配置位置与 `CBGSETG` 相同——画面左下角与图像左下角对齐。
- 颜色的 alpha 值不是 255（透明或半透明）的像素不会被识别为按钮值。

返回值：设置成功返回 `1`；指定的 Graphics 未创建或已废弃（`IsCreated == false` 或 `Bitmap == null`）时返回 `0`；Graphics ID 为负或过大时抛出 CodeEE；重复把同一个 Graphics 设为按钮映射时返回 `0`（源码行为，见伪代码）。

## 用法

### int CBGSETBMAPG(int Graphics ID)
- `Graphics ID`：已由 `GCREATE` 等创建的 Graphics 的 ID（0 以上整数）。为负时抛出 CodeEE，超过 int.MaxValue 时抛出 CodeEE。
- 返回值：成功 `1`，失败 `0`。
```erb
; 创建按钮映射并配合 CBGSETBUTTONSPRITE 使用
GCREATE 0, 256, 256
; ……向 Graphics 0 绘制按钮分区颜色……
IF CBGSETBMAPG(0)
	CBGSETBUTTONSPRITE 1, "按钮通常", "按钮按下", 0, 0, 1
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:202`（`["CBGSETBMAPG"] = new CBGSetBMapGMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6665`（`CBGSetBMapGMethod`）
- 辅助：`Runtime/Script/Statements/Function/Creator.Method.cs:5159`（`ReadGraphics`）、`UI/Game/EmueraConsole.cs:224`（`CBG_SetButtonMap`）

```text
CBGSetBMapGMethod:
构造：返回类型 = long；参数 = [long]；CanRestructure = false。
GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛出 CodeEE（该函数只能在 GDI+（GRAPHICS/TEXTRENDERER）绘制方式下使用）
    g ← ReadGraphics(Name, exm, args, 0)
        ; ID 为负 → CodeEE"GraphicsID 不能为负"
        ; ID > int.MaxValue → CodeEE"GraphicsID 过大"
        ; 否则返回 AppContents.GetGraphics((int)ID)，可能为 null
    若 !g.IsCreated 或 g.Bitmap == null:
        返回 0
    返回 exm.Console.CBG_SetButtonMap(g)

EmueraConsole.CBG_SetButtonMap(gra):
    若 gra == null 或 !gra.IsCreated: 返回 false
    若 cbgButtonMap == gra: 返回 false       ; 与当前映射相同则不更新
    cbgButtonMap ← gra
    selectingCBGButtonInt ← -1               ; 选中状态复位
    lastSelectingCBGButtonInt ← -1
    返回 true
```

## 备注

- 类 XML 注释写的是 `CBGSETBMAPG(int ID, int x, int y, int zdepth)`，实际只接受 1 个参数（`argumentTypeArray = [long]`），注释为过时残留。
- 文档未提及「重复设置同一 Graphics 返回 0」的源码行为。
- 参数范围校验（负 ID 等）不在文档中，来自源码。
