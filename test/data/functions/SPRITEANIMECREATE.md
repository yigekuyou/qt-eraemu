# SPRITEANIMECREATE

- **类别**：式中函数（图像处理相关；另有同名命令形态）
- **签名**：int SPRITEANIMECREATE(str spriteName, int width, int height)
- **文档来源**：`ecd/Command.md`「### SPRITEANIMECREATE `<精灵名>`, `<宽度>`, `<高度>`」（命令形态，按指令记载）；`ecd/Expression.md` 未收录；`ecd/ERB_Commands.md` 归入「图像处理相关」

## 语义

创建一个动画精灵（animation sprite）：资源名为 `spriteName`，尺寸为 `width × height`。创建成功返回非 0（源码固定返回 1）；因同名资源已存在等原因失败时返回 0。

创建的动画精灵本身不包含画面内容，需要用 `SPRITEANIMEADDFRAME` 逐帧添加。创建后可像 `resources` 文件夹 CSV 中声明的资源一样使用（如 `PRINT_IMG`、`HTML_PRINT` 的 img 标签等）。

限制：仅当绘制方式为 `GRAPHICS` 或 `TEXTRENDERER` 时可用——绘制方式为 WINAPI 时抛出 CodeEE。`width`、`height` 必须 > 0 且不超过 `AbstractImage.MAX_IMAGESIZE`，否则抛出 CodeEE（不是返回 0）。

同名命令形态 `SPRITEANIMECREATE <精灵名>, <宽度>, <高度>` 与函数形态共用同一实现类（public），本文档以式中函数形态为主。

## 用法

### int SPRITEANIMECREATE(str spriteName, int width, int height)
- `spriteName`：精灵资源名；空字符串时直接失败返回 0。
- `width`：精灵宽度，1 ～ MAX_IMAGESIZE，否则抛 CodeEE。
- `height`：精灵高度，1 ～ MAX_IMAGESIZE，否则抛 CodeEE。
- 返回值：成功 `1`；同名资源已存在等失败时 `0`。
```erb
	IF SPRITEANIMECREATE("walk", 64, 64) != 0
		PRINTL 动画精灵创建成功，可以开始添加帧。
	ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:209`（`["SPRITEANIMECREATE"] = new SpriteAnimeCreateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6467`（`SpriteAnimeCreateMethod`，public）

```text
构造：返回类型 = long；参数 = [string, long, long]；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛出 CodeEE（"该指令仅限 GDI+（GRAPHICS/TEXTRENDERER）绘制方式"）
    imgname ← args[0].GetStrValue(exm)
    若 imgname 为空: 返回 0
    若 AppContents.GetSprite(imgname) 已存在且 IsCreated: 返回 0   ; 同名资源已存在则失败
    pos ← ReadPoint(Name, exm, args, 1)        ; 取第 2、3 参数为 (width, height)
    若 pos.X <= 0:                    抛出 CodeEE（"Width 指定了 0 以下的值"）
    若 pos.X > AbstractImage.MAX_IMAGESIZE: 抛出 CodeEE（"Width 指定了过大的值"）
    若 pos.Y <= 0:                    抛出 CodeEE（"Height 指定了 0 以下的值"）
    若 pos.Y > AbstractImage.MAX_IMAGESIZE: 抛出 CodeEE（"Height 指定了过大的值"）
    AppContents.CreateSpriteAnime(imgname, pos.X, pos.Y)
    返回 1
```

## 备注

- ecd/Command.md 按命令形态记载（未写返回值类型名，只说「创建成功时返回非 0，失败时返回 0」）；源码成功时固定返回 1。
- 文档只说失败返回 0；源码中尺寸非法（≤0 或超过 MAX_IMAGESIZE）和绘制方式为 WINAPI 时是抛 CodeEE 而不是返回 0——文档未区分这两类失败。
- 动画精灵注意事项见 ecd/Command.md 引用的「资源文件」（Resource.md）一节。
- zh 套件未收录本函数。
