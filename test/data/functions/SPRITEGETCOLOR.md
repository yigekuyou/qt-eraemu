# SPRITEGETCOLOR

- **类别**：式中函数
- **签名**：
  - int SPRITEGETCOLOR(str spriteName, int x, int y)
- **文档来源**：`ecd/docs/translation/Command.md`「SPRITEGETCOLOR `<精灵名>`, `<x>`, `<y>`」小节；zh 套件未收录独立小节

## 语义

以 `0xAARRGGBB` 形式的整数值获取精灵中 (x, y) 处的颜色（含 alpha 通道）。

失败时返回 **-1**（不是 0）：精灵未创建或已废弃、或 x、y 超出精灵图像范围。取得「黑色且完全透明」位置颜色时返回 0——这是它与失败值的区别，文档特别提醒只有本函数失败时返回 -1。

ecd 文档按指令形式记载于图像处理组，源码中实为式中函数。

## 用法

### SPRITEGETCOLOR(spriteName, x, y)
- spriteName：精灵资源名（字符串）。
- x, y：精灵坐标系中的像素位置，左上角为 (0, 0)。
```erb
c = SPRITEGETCOLOR("MYSPRITE", 5, 5)
IF c < 0
	PRINTL 读取颜色失败（精灵不存在或坐标越界）
ELSE
	PRINTFORML A=%TOSTR(c >> 24 & 0xFF, "{0:X2}")% R=%TOSTR(c >> 16 & 0xFF, "{0:X2}")%
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:176`（`["SPRITEGETCOLOR"] = new SpriteGetColorMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5889`（`sealed class SpriteGetColorMethod : FunctionMethod`，`GetIntValue`）；辅助：同文件 `ReadPoint`(:5199)、`UI/Game/Image/CroppedImage.cs` 中 `ASprite.SpriteGetColor`

```text
函数 SPRITEGETCOLOR(参数表):
    imgname <- 参数[0] 的字符串值
    img <- AppContents.GetSprite(imgname)
    # 实现注释：与其他函数不同，失败返回负值而非 0
    若 img == null 或 !img.IsCreated:
        返回 -1
    p <- ReadPoint(参数[1..2])
        # x、y 各自超出 int 范围时抛 CodeEE
    若 p.X < 0 或 p.X >= img.DestBaseSize.Width:
        返回 -1
    若 p.Y < 0 或 p.Y >= img.DestBaseSize.Height:
        返回 -1
    c <- img.SpriteGetColor(p.X, p.Y)
        # ASpriteSingle 版：取父位图 GetPixel(x + SrcRectangle.X, y + SrcRectangle.Y)
        # 父图像未创建或换算后坐标越界时返回 Color.Transparent
    返回 c.ToArgb() & 0xFFFFFFFFL    # 转为无符号 0xAARRGGBB 的 long
```

## 备注

- 返回值语义：源码返回 `c.ToArgb() & 0xFFFFFFFFL`，即 alpha 在最高 8 位的无符号整数；透明黑为 0，与文档「失败返回 -1 而非 0」的提醒一致。
- 越界检查基于 `DestBaseSize`（精灵输出尺寸，构造时取矩形宽高的绝对值）。
- ecd 把它按指令形式记载，源码中为式中函数；文档与源码无其他冲突。
