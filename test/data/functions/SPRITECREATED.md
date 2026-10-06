# SPRITECREATED

- **类别**：式中函数
- **签名**：
  - int SPRITECREATED(str spriteName)
- **文档来源**：`ecd/docs/translation/Command.md`「SPRITECREATED `<精灵名>`」小节；zh 套件 Resource.md「图元（sprite）」一节有交叉印证

## 语义

判断指定名字的精灵是否已创建。已创建返回 1，未创建或已废弃返回 0。常用于区分 `SPRITEPOSX`/`SPRITEPOSY` 返回 0 是「真实位置为 0」还是「精灵不存在」。

zh 的 Resource.md 提到 resources CSV 中声明的资源也可以用 `SPRITECREATED("资源名称A")` 的形式使用，即对静态声明的精灵同样有效。

## 用法

### SPRITECREATED(spriteName)
- spriteName：精灵资源名（字符串）。
```erb
IF SPRITECREATED("MYSPRITE")
	PRINTFORML 精灵存在，宽度 {SPRITEWIDTH("MYSPRITE")}
ELSE
	PRINTL 精灵不存在
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:150`（`["SPRITECREATED"] = new SpriteStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5830`（`sealed class SpriteStateMethod : FunctionMethod`，`GetIntValue`）；辅助：`UI/Game/Image/AppContents.cs:44`（GetSprite）

```text
函数 SPRITECREATED(参数表):        # SpriteStateMethod 按 Name 分发
    imgname <- 参数[0] 的字符串值
    img <- AppContents.GetSprite(imgname)
        # 名字转大写后查 imageDictionary；name 为 null 或查不到返回 null
    若 img == null 或 !img.IsCreated:
        返回 0
    本函数 Name == "SPRITECREATED":
        返回 1
```

## 备注

- ecd Command.md 以指令形式记载本组图像函数，源码中实为式中函数（返回值直接参与表达式）。
- SPRITECREATED 与 SPRITEWIDTH、SPRITEHEIGHT、SPRITEPOSX、SPRITEPOSY 共用同一个 `SpriteStateMethod` 类，按注册名 `Name` 分支，故注册行号一致。
- 与文档无冲突。
