# SPRITEHEIGHT

- **类别**：式中函数
- **签名**：
  - int SPRITEHEIGHT(str spriteName)
- **文档来源**：`ecd/docs/translation/Command.md`「SPRITEHEIGHT `<精灵名>`」小节；zh 套件未收录独立小节

## 语义

获取指定名字精灵的高度（像素）。精灵未创建或已废弃时返回 0。

在 ecd Command.md 的 GDRAWSPRITE 小节中也作为提示出现：「精灵的尺寸可以用 SPRITEWIDTH(str imgName)、SPRITEHEIGHT(str imgName) 函数获取」。与 SPRITECREATED 搭配可区分「高度为 0」与「精灵不存在」。

## 用法

### SPRITEHEIGHT(spriteName)
- spriteName：精灵资源名（字符串）。
```erb
GCREATE 0, 100, 50
SPRITECREATE("MYSPRITE", 0)
PRINTFORML 高度 = {SPRITEHEIGHT("MYSPRITE")}   ;输出 50
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:152`（`["SPRITEHEIGHT"] = new SpriteStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5830`（`sealed class SpriteStateMethod : FunctionMethod`，`GetIntValue`）；辅助：`UI/Game/Image/AppContents.cs:44`（GetSprite）、`UI/Game/Image/CroppedImage.cs` 的 `ASprite.DestBaseSize`

```text
函数 SPRITEHEIGHT(参数表):        # SpriteStateMethod 按 Name 分发
    imgname <- 参数[0] 的字符串值
    img <- AppContents.GetSprite(imgname)
    若 img == null 或 !img.IsCreated:
        返回 0
    本函数 Name == "SPRITEHEIGHT":
        返回 img.DestBaseSize.Height
        # DestBaseSize 在 ASprite 构造时由切取矩形尺寸决定，负值会被取绝对值
```

## 备注

- SPRITECREATED、SPRITEWIDTH、SPRITEHEIGHT、SPRITEPOSX、SPRITEPOSY 共用 `SpriteStateMethod`，按注册名分支。
- ecd 把它按指令形式记载，源码中为式中函数；文档与源码无冲突。
