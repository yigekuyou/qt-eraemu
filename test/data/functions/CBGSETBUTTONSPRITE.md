# CBGSETBUTTONSPRITE

- **类别**：式中函数（CBG 系图像处理指令，可作命令或函数使用）
- **签名**：int CBGSETBUTTONSPRITE(int 按钮值, str 精灵名, str 选中精灵名, int x, int y, int z深度)
- **签名**：int CBGSETBUTTONSPRITE(int 按钮值, str 精灵名, str 选中精灵名, int x, int y, int z深度, str 工具提示)
- **文档来源**：`ecd/Command.md`「### CBGSETBUTTONSPRITE `<按钮值>`, `<精灵名>`, `<选中精灵名>`, `<x>`, `<y>`, `<z深度>`」与「### CBGSETBUTTONSPRITE `..., <工具提示>`」（图像处理相关章节，两种签名分列两节）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

与 `CBGSETBMAPG` 指令设置的按钮映射联动，在客户端区域设置可选择按钮。仅当绘制方式为 `GRAPHICS` 或 `TEXTRENDERER` 时可用，绘制方式为 `WINAPI` 时出错。

- 鼠标正下方的按钮映射图像颜色的 `0xRRGGBB` 值等于参数 `button` 时显示 `选中精灵名`，否则显示 `精灵名`。
- `精灵名` 或 `选中精灵名` 可以指定空字符串，此时未选中或选中时不显示任何内容。
- x、y、zdepth 与 `CBGSETSPRITE` 相同。基准位置 `(x,y) = (0,0)` 是画面左下角与图像左下角对齐的位置；zdepth 必须为非 0 的 int 值。
- 可通过可选的第 7 参数 `工具提示`（tooltipmes）指定选中该按钮时显示的工具提示字符串。
- 同一个 `button` 值可以分配多个 `CBGSETBUTTONSPRITE`，也不必与按钮位置一致。此时工具提示与图像的 xy 位置无关，优先显示设置了工具提示字符串中 zdepth 最大（最先绘制、看起来最靠里）的那个。

返回值：设置成功返回 `1`；`button` 值不在 `0 ～ 0xFFFFFF` 范围、精灵获取失败等情况下返回 `0`。

## 用法

### int CBGSETBUTTONSPRITE(int 按钮值, str 精灵名, str 选中精灵名, int x, int y, int z深度)
- `按钮值`：与按钮映射颜色 `0xRRGGBB` 对应的按钮值，须在 `0 ～ 0xFFFFFF` 内。
- `精灵名`：未选中时显示的精灵名；空字符串表示不显示。
- `选中精灵名`：选中时显示的精灵名；空字符串表示不显示。
- `x`、`y`：显示位置偏移（int 范围，超出抛出 CodeEE）。
- `z深度`：绘制深度，非 0 的 int 值；为 0 或超出 int 范围时抛出 CodeEE。
- 返回值：成功 `1`，失败 `0`。

### int CBGSETBUTTONSPRITE(int 按钮值, str 精灵名, str 选中精灵名, int x, int y, int z深度, str 工具提示)
- 第 7 参数 `工具提示`：选中该按钮时显示的工具提示字符串，可省略（省略时为 null，即无工具提示）。
- 其余参数与上一用法相同。
```erb
GCREATE 0, 256, 256
; ……在 Graphics 0 中以颜色 0x000001 画按钮区域……
CBGSETBMAPG(0)
; 无工具提示版本
CBGSETBUTTONSPRITE(1, "按钮通常", "按钮按下", 100, 100, 1)
; 带工具提示版本
CBGSETBUTTONSPRITE(2, "设置通常", "设置按下", 200, 100, 1, "打开设置")
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:203`（`["CBGSETBUTTONSPRITE"] = new CBGSETButtonSpriteMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6723`（`CBGSETButtonSpriteMethod`，内部注释名 CBGSETBUTTONCIMG）
- 辅助：`Runtime/Script/Statements/Function/Creator.Method.cs:5199`（`ReadPoint`）
- 控制台侧：`UI/Game/EmueraConsole.cs:236`（`CBG_SetButtonImage`）

```text
CBGSETButtonSpriteMethod:
构造：返回类型 = long；
     argumentTypeArrayEx = [{ Int, String, String, Int, Int, Int, String }, OmitStart = 6]
     （第 7 参数可省略）；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛出 CodeEE（只能在 GDI+ 绘制方式下使用）
    b64 ← args[0].GetIntValue(exm)
    若 b64 < 0 或 b64 > 0xFFFFFF: 返回 0        ; 按钮值超出 24 位颜色范围
    imgnameN ← args[1].GetStrValue(exm)
    imgN ← AppContents.GetSprite(imgnameN)      ; 允许为 null（空精灵名）
    imgnameB ← args[2].GetStrValue(exm)
    imgB ← AppContents.GetSprite(imgnameB)
    p ← ReadPoint(Name, exm, args, 3)           ; x、y 超出 int 范围 → CodeEE
    z64 ← args[5].GetIntValue(exm)
    若 z64 超出 int 范围 或 z64 == 0:
        抛出 CodeEE（第 6 参数超出范围，且不能为 0）
    tooltip ← null
    若 args.Count > 6: tooltip ← args[6].GetStrValue(exm)
    若 !exm.Console.CBG_SetButtonImage((int)b64, imgN, imgB, p.X, p.Y, (int)z64, tooltip):
        返回 0
    返回 1

EmueraConsole.CBG_SetButtonImage(buttonValue, imageN, imageB, x, y, zdepth, tooltip):
    若 zdepth == 0: 抛出 ArgumentOutOfRangeException
    cbgList.Add(new ClientBackGroundImage(zdepth){
        Img = imageN, ImgB = imageB, x, y,
        isButton = true, buttonValue = buttonValue, tooltipString = tooltip })
    cbgList.Sort()      ; 按 zdepth 降序
    返回 true
```

## 备注

- 与 `CBGSETSPRITE`/`CBGSETG` 不同：按钮条目不要求精灵已创建（GetSprite 结果可为 null，由空字符串语义配合），也无需先判定 `IsCreated`。
- 实现类 XML 注释写的是 `CBGSETBUTTONCIMG(int button, str imgName, str imgName, int x, int y, int zdepth, str tooltipmes)`，与注册名 CBGSETBUTTONSPRITE 为同一函数。
- 按钮值的有效范围（0～0xFFFFFF，超出返回 0 而非报错）来自源码，文档未提及。
