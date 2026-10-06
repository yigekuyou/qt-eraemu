# SPRITEANIMEADDFRAME

- **类别**：式中函数（图像处理相关；另有同名命令形态）
- **签名**：int SPRITEANIMEADDFRAME(str spriteName, int graphID, int x, int y, int width, int height, int offsetx, int offsety, int delay)
- **文档来源**：`ecd/Command.md`「### SPRITEANIMEADDFRAME `<精灵名>`, `<Graphics ID>`, `<x>`, `<y>`, `<宽度>`, `<高度>`, `<偏移X>`, `<偏移Y>`, `<延迟>`」（命令形态，按指令记载）；`ecd/Expression.md` 未收录；`ecd/ERB_Commands.md` 归入「图像处理相关」

## 语义

为 `spriteName` 指定资源名的动画精灵添加一帧。把 `graphID` 指定 Graphics 中 `(x, y, width, height)` 矩形作为帧图像，放置在精灵内距左上角 `(offsetx, offsety)` 的位置；超出动画精灵创建时设定尺寸范围的部分不会被绘制。`delay` 指定该帧显示的毫秒数。

添加帧成功返回 `1`，失败返回 `0`。失败情形（源码逐条）：`spriteName` 的资源不存在、不是动画精灵（或已废弃）、指定的 Graphics 未创建、矩形非法（宽/高 ≤ 0，或超出父 Graphics 范围）、`delay` ≤ 0 或超过 int.MaxValue、`spriteName` 为空字符串。此时什么都不做。

限制：仅当绘制方式为 `GRAPHICS` 或 `TEXTRENDERER` 时可用——绘制方式为 WINAPI 时抛出 CodeEE。

## 用法

### int SPRITEANIMEADDFRAME(str spriteName, int graphID, int x, int y, int width, int height, int offsetx, int offsety, int delay)
- `spriteName`：目标动画精灵资源名（须已由 `SPRITEANIMECREATE` 创建）。
- `graphID`：作为帧图像来源的 Graphics 编号。
- `x`、`y`、`width`、`height`：从该 Graphics 切取的矩形（须为正且完全位于父图像内）。
- `offsetx`、`offsety`：帧在精灵内的放置偏移（相对精灵左上角）。
- `delay`：该帧显示时长，毫秒（须 > 0）。
- 返回值：成功 `1`，失败 `0`。
```erb
	SPRITEANIMECREATE("walk", 64, 64)
	FOR LOCAL, 0, 4
		SPRITEANIMEADDFRAME("walk", 10, LOCAL * 64, 0, 64, 64, 0, 0, 250)
	NEXT
	SETANIMETIMER(50)	; 让 INPUT 期间重绘
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:210`（`["SPRITEANIMEADDFRAME"] = new SpriteAnimeAddFrameMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6509`（`SpriteAnimeAddFrameMethod`，public）

```text
构造：返回类型 = long；参数 = [string, long × 8]（共 9 个）；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛出 CodeEE（"该指令仅限 GDI+（GRAPHICS/TEXTRENDERER）绘制方式"）
    imgname ← args[0].GetStrValue(exm)
    若 imgname 为空: 返回 0
    若 AppContents.GetSprite(imgname) == null: 返回 0        ; 资源不存在
    img ← (SpriteAnime)GetSprite(imgname)
    若 img == null 或 !img.IsCreated: 返回 0                  ; 不是动画精灵或已废弃
    g ← ReadGraphics(Name, exm, args, 1)                     ; 第 2 参数为 Graphics ID
    若 !g.IsCreated: 返回 0
    rect ← ReadRectangle(Name, exm, args, 2)                 ; 第 3～6 参数为 (x, y, w, h)
    若 rect.Width <= 0 或 rect.Height <= 0
       或 rect.X < 0 或 rect.X + rect.Width > g.Width
       或 rect.Y < 0 或 rect.Y + rect.Height > g.Height:
        返回 0                                               ; 矩形非法 / 越出父图像
    offset ← ReadPoint(Name, exm, args, 6)                   ; 第 7、8 参数为 (offsetx, offsety)
    delay ← args[8].GetIntValue(exm)
    若 delay <= 0 或 delay > int.MaxValue: 返回 0
    img.AddFrame(g, rect, offset, (int)delay)
    返回 1
```

## 备注

- ecd/Command.md 按命令形态记载，与源码一致处：失败返回 0、成功返回 1（文档表述为「添加帧成功时返回 1，失败时返回 0」）；绘制方式限制（GRAPHICS/TEXTRENDERER）文档有说明，源码中对应 WINAPI 时抛 CodeEE。
- 源码有一处小瑕疵：`if (img == null && !img.IsCreated)` 中当 `img == null` 时会先发生空引用判断（前一行已单独判 null 并 return，故实际不触发）；伪代码按语义等价拆分记录。
- 必须先有 `SPRITEANIMECREATE` 创建的同名动画精灵，否则一律返回 0；重绘需配合 `SETANIMETIMER`。
- zh 套件未收录本函数。
