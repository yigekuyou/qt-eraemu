# SPRITEPOSY

- **类别**：式中函数
- **签名**：
  - int SPRITEPOSY(str spriteName)
- **文档来源**：`ecd/docs/translation/Command.md`「SPRITEPOSY `<精灵名>`」小节；zh 套件 Resource.md 提及精灵有 posy 相对位置（未单列本函数）

## 语义

获取指定名字精灵相对位置的 Y 值。精灵未创建或已废弃时返回 0。

与 `SPRITEPOSX` 一样，返回 0 既可能是真实位置为 0，也可能是精灵不存在；需要区分时应另外调用 `SPRITECREATED`。

## 用法

### SPRITEPOSY(spriteName)
- spriteName：精灵资源名（字符串）。
```erb
IF SPRITECREATED("MYSPRITE")
	PRINTFORML 相对位置 = ({SPRITEPOSX("MYSPRITE")}, {SPRITEPOSY("MYSPRITE")})
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:156`（`["SPRITEPOSY"] = new SpriteStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5830`（`sealed class SpriteStateMethod : FunctionMethod`，`GetIntValue`）；辅助：`UI/Game/Image/AppContents.cs:44`（GetSprite）、`UI/Game/Image/CroppedImage.cs` 的 `ASprite.DestBasePosition`

```text
函数 SPRITEPOSY(参数表):        # SpriteStateMethod 按 Name 分发
    imgname <- 参数[0] 的字符串值
    img <- AppContents.GetSprite(imgname)
    若 img == null 或 !img.IsCreated:
        返回 0
    本函数 Name == "SPRITEPOSY":
        返回 img.DestBasePosition.Y
```

## 备注

- SPRITECREATED、SPRITEWIDTH、SPRITEHEIGHT、SPRITEPOSX、SPRITEPOSY 共用 `SpriteStateMethod`，按注册名分支。
- ecd 把它按指令形式记载，源码中为式中函数；文档与源码无冲突。
