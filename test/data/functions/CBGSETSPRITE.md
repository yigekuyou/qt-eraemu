# CBGSETSPRITE

- **类别**：式中函数（CBG 系图像处理指令，可作命令或函数使用）
- **签名**：int CBGSETSPRITE(str 精灵名, int x, int y, int z深度)
- **文档来源**：`ecd/Command.md`「### CBGSETSPRITE `<精灵名>`, `<x>`, `<y>`, `<z深度>`」（图像处理相关章节）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

把 `精灵名`（`spriteName`）指定资源名的精灵设置为显示在客户端区域的背景图像。精灵与在 `resources` 文件夹中声明的资源或 `SPRITECREATE` 创建的精灵一样使用。

- `(x, y)` 指定 0 时，客户端区域的左下角与图像的左下角对齐显示。
- x 以右方向为正，y 以下方向为正，zdepth 以画面纵深方向为正。
- zdepth 必须指定 0 以外的值：普通文字绘制相当于 `zdepth == 0`；zdepth 为负时绘制在文字之前（更靠前）。zdepth 为 0 或超出 int 范围时抛出 CodeEE。

返回值：设置成功返回 `1`；精灵不存在或未创建时返回 `0`；`CBG_SetImage` 返回 false 时也返回 `0`。

与 `CBGSETG` 的区别：本函数取的是具名精灵（ASprite），`CBGSETG` 取的是 Graphics 并包成无名精灵。

## 用法

### int CBGSETSPRITE(str 精灵名, int x, int y, int z深度)
- `精灵名`：精灵的资源名字符串。不存在或未创建时返回 `0`（不报错）。
- `x`、`y`：显示位置偏移（int 范围，超出抛出 CodeEE）。
- `z深度`：绘制深度，非 0 的 int 值；为 0 或超出 int 范围时抛出 CodeEE。
- 返回值：成功 `1`，失败 `0`。
```erb
; 显示资源精灵作为背景，深度 1
IF CBGSETSPRITE("背景图", 0, 0, 1)
	PRINTL 背景精灵设置成功
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:196`（`["CBGSETSPRITE"] = new CBGSetCIMGMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6691`（`CBGSetCIMGMethod`，内部名 CBGSETCIMG）
- 辅助：`Runtime/Script/Statements/Function/Creator.Method.cs:5199`（`ReadPoint`）
- 控制台侧：`UI/Game/EmueraConsole.cs:207`（`CBG_SetImage`）

```text
CBGSetCIMGMethod:
构造：返回类型 = long；参数 = [string, long, long, long]；CanRestructure = false。
GetIntValue(exm, args):
    ; 注意：本函数不检查绘制方式是否为 WINAPI（源码中该检查被注释掉）
    imgname ← args[0].GetStrValue(exm)
    img ← AppContents.GetSprite(imgname)
    若 img == null 或 !img.IsCreated: 返回 0
    p ← ReadPoint(Name, exm, args, 1)      ; x、y 超出 int 范围 → CodeEE
    z64 ← args[3].GetIntValue(exm)
    若 z64 超出 int 范围 或 z64 == 0:
        抛出 CodeEE（第 4 参数超出范围，且不能为 0）
    若 !exm.Console.CBG_SetImage(img, p.X, p.Y, (int)z64):
        返回 0
    返回 1

EmueraConsole.CBG_SetImage(image, x, y, zdepth):
    若 image == null 或 !image.IsCreated: 返回 false
    若 zdepth == 0: 抛出 ArgumentOutOfRangeException
    cbgList.Add(new ClientBackGroundImage(zdepth){ Img = image, x, y })
    cbgList.Sort()      ; 按 zdepth 降序
    返回 true
```

## 备注

- 注册键为 `CBGSETSPRITE`，实现类名与内部 XML 注释为 `CBGSETCIMG`（旧名 `CBG_SET_CIMG`），语义相同。
- 与 `CBGSETG` 不同，本函数不检查 WINAPI 绘制方式（源码检查被注释掉）；绘制方式的实际限制由底层渲染决定。
- 具名精灵（Name 非空）被 `CBGCLEAR` 等删除时不会 Dispose（只清理无名一次性图像）。
