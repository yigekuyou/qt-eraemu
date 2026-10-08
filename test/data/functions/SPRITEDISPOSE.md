# SPRITEDISPOSE

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数
- **签名**：
  - int SPRITEDISPOSE(str spriteName)
- **文档来源**：`ecd/docs/translation/Command.md`「SPRITEDISPOSE `<精灵名>`」小节；zh 套件未收录独立小节

## 语义

废弃（dispose）spriteName 指定资源名的精灵。废弃成功返回非 0（1）；精灵不存在或已废弃时返回 0。

该操作只废弃精灵本身，不影响其父 Graphics；要释放 Graphics 占用的内存需另用 `GDISPOSE`。废弃后该精灵从资源字典中移除，`SPRITECREATED` 将返回 0。

ecd 文档按指令形式记载于图像处理组，源码中实为式中函数。

## 用法

### SPRITEDISPOSE(spriteName)
- spriteName：精灵资源名（字符串，内部转大写后查找）。
```erb
SPRITECREATE("TMP", 0)
SPRITEDISPOSE("TMP")   ;废弃后 SPRITECREATED("TMP") 变为 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:193`（`["SPRITEDISPOSE"] = new SpriteDisposeMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6135`（`sealed class SpriteDisposeMethod : FunctionMethod`，`GetIntValue`）；辅助：`UI/Game/Image/AppContents.cs:54`（SpriteDispose）

```text
函数 SPRITEDISPOSE(参数表):
    imgname <- 参数[0] 的字符串值
    img <- AppContents.GetSprite(imgname)   # 名字转大写后查表
    若 img == null 或 !img.IsCreated:
        返回 0
    AppContents.SpriteDispose(imgname):
        # imgname 转大写；从 imageDictionary 查到后调用 value.Dispose()
        # （SpriteG 系将 BaseImage 置 null），再从字典中移除该条目
        # name 为 null 或查不到时直接返回（不报错）
    返回 1
```

## 备注

- 文档与源码一致。注意本仓库另有 `SPRITEDISPOSEALL(int delCsvImage)`（式中函数，`Runtime/Script/Statements/Function/Creator.cs:330`，不在本批范围内），可一次性清除全部精灵，参数非 0 时连 resources CSV 声明的精灵也一并清除。
- ecd 把它按指令形式记载，源码中为式中函数。
