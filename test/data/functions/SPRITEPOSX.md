# SPRITEPOSX

- **类别**：式中函数
- **签名**：
  - int SPRITEPOSX(str spriteName)
- **文档来源**：`ecd/docs/translation/Command.md`「SPRITEPOSX `<精灵名>`」小节；zh 套件 Resource.md 提及精灵有 posx 相对位置（未单列本函数）

## 语义

获取指定名字精灵相对位置的 X 值。精灵未创建或已废弃时返回 0。

由于「相对位置 X 为 0」与「未创建或已废弃」都返回 0，需要区分时应另外调用 `SPRITECREATED`（文档明确提醒）。

## 用法

### SPRITEPOSX(spriteName)
- spriteName：精灵资源名（字符串）。
```erb
IF SPRITECREATED("MYSPRITE")
	PRINTFORML 相对位置 = ({SPRITEPOSX("MYSPRITE")}, {SPRITEPOSY("MYSPRITE")})
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:155`（`["SPRITEPOSX"] = new SpriteStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5830`（`sealed class SpriteStateMethod : FunctionMethod`，`GetIntValue`）；辅助：`UI/Game/Image/AppContents.cs:44`（GetSprite）、`UI/Game/Image/CroppedImage.cs` 的 `ASprite.DestBasePosition`

```text
函数 SPRITEPOSX(参数表):        # SpriteStateMethod 按 Name 分发
    imgname <- 参数[0] 的字符串值
    img <- AppContents.GetSprite(imgname)
    若 img == null 或 !img.IsCreated:
        返回 0
    本函数 Name == "SPRITEPOSX":
        返回 img.DestBasePosition.X
```

## 备注

- SPRITECREATED、SPRITEWIDTH、SPRITEHEIGHT、SPRITEPOSX、SPRITEPOSY 共用 `SpriteStateMethod`，按注册名分支。
- ecd 把它按指令形式记载，源码中为式中函数；文档与源码无冲突。
