# SPRITESETPOS

- **类别**：式中函数
- **签名**：
  - int SPRITESETPOS(str spriteName, int posX, int posY)
- **文档来源**：`ecd/docs/translation/Command.md`「SPRITESETPOS `<精灵名>`, `<位置X>`, `<位置Y>`」小节；zh 套件未收录独立小节

## 语义

设置指定名字精灵的相对位置（DestBasePosition）为 (posX, posY)（绝对赋值，非累加）。设置成功返回非 0（1）；精灵未创建或已废弃时返回 0，什么都不做。

相对位置影响精灵显示时的偏移（按同比例调整缩放输出）。ecd 文档按指令形式记载于图像处理组，源码中实为式中函数。

## 用法

### SPRITESETPOS(spriteName, posX, posY)
- spriteName：精灵资源名（字符串）。
- posX, posY：新的相对位置（可正可负）。
```erb
SPRITECREATE("MYSPRITE", 0)
SPRITESETPOS("MYSPRITE", 100, 50)
;以下两者等价：
SPRITEMOVE("MYSPRITE", 5, 5)
SPRITESETPOS("MYSPRITE", SPRITEPOSX("MYSPRITE") + 5, SPRITEPOSY("MYSPRITE") + 5)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:154`（`["SPRITESETPOS"] = new SpriteSetPosMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5861`（`sealed class SpriteSetPosMethod : FunctionMethod`，`GetIntValue`）；辅助：同文件 `ReadPoint`(:5199)、`UI/Game/Image/CroppedImage.cs` 的 `ASprite.DestBasePosition`

```text
函数 SPRITESETPOS(参数表):        # SpriteSetPosMethod 按 Name 分发
    imgname <- 参数[0] 的字符串值
    img <- AppContents.GetSprite(imgname)
    若 img == null 或 !img.IsCreated:
        返回 0
    p <- ReadPoint(参数[1..2])
        # posX、posY 各自超出 int 范围时抛 CodeEE
    本函数 Name == "SPRITESETPOS":
        img.DestBasePosition <- p        # 直接替换 Point
        返回 1
```

## 备注

- SPRITEMOVE 与 SPRITESETPOS 共用 `SpriteSetPosMethod` 类，按注册名分支：SETPOS 直接赋值，MOVE 偏移累加。
- ecd 把它按指令形式记载，源码中为式中函数；文档与源码无冲突。
