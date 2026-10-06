# SPRITEMOVE

- **类别**：式中函数
- **签名**：
  - int SPRITEMOVE(str spriteName, int moveX, int moveY)
- **文档来源**：`ecd/docs/translation/Command.md`「SPRITEMOVE `<精灵名>`, `<移动X>`, `<移动Y>`」小节；zh 套件 Resource.md 提及「这些值可以通过 SPRITEPOS 和 SPRITEMOVE 指令动态地改变」

## 语义

把指定名字精灵的相对位置（DestBasePosition）的 X、Y 各加上指定值（相对移动）。移动成功返回非 0（1）；精灵未创建或已废弃时返回 0，什么都不做。

等价于：
```
SPRITESETPOS spriteName, SPRITEPOSX(spriteName) + moveX, SPRITEPOSY(spriteName) + moveY
```

相对位置影响精灵显示时的偏移（按同比例调整缩放输出）。ecd 文档按指令形式记载于图像处理组，源码中实为式中函数。

## 用法

### SPRITEMOVE(spriteName, moveX, moveY)
- spriteName：精灵资源名（字符串）。
- moveX, moveY：位置的增量（可正可负）。
```erb
SPRITECREATE("MYSPRITE", 0)
SPRITEMOVE("MYSPRITE", 10, -5)   ;相对位置变为 (10, -5)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:153`（`["SPRITEMOVE"] = new SpriteSetPosMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5861`（`sealed class SpriteSetPosMethod : FunctionMethod`，`GetIntValue`）；辅助：同文件 `ReadPoint`(:5199)、`UI/Game/Image/CroppedImage.cs` 的 `ASprite.DestBasePosition`

```text
函数 SPRITEMOVE(参数表):        # SpriteSetPosMethod 按 Name 分发
    imgname <- 参数[0] 的字符串值
    img <- AppContents.GetSprite(imgname)
    若 img == null 或 !img.IsCreated:
        返回 0
    p <- ReadPoint(参数[1..2])
        # moveX、moveY 各自超出 int 范围时抛 CodeEE
    本函数 Name == "SPRITEMOVE":
        img.DestBasePosition.Offset(p)   # 原地累加，等价 ASprite.Move(point)
        返回 1
```

## 备注

- SPRITEMOVE 与 SPRITESETPOS 共用 `SpriteSetPosMethod` 类，按注册名分支：MOVE 是「偏移累加」，SETPOS 是「直接赋值」。
- zh 的 Resource.md 中出现的「SPRITEPOS」指令在本仓库源码中不存在（未注册），实际可用的是 SPRITEPOSX/SPRITEPOSY（读取）与 SPRITESETPOS/SPRITEMOVE（设置）。
- ecd 把它按指令形式记载，源码中为式中函数；文档与源码无其他冲突。
