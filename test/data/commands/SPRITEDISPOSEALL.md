# SPRITEDISPOSEALL

- **类别**：EE 扩展命令
- **签名**：
  - `SPRITEDISPOSEALL <数值表达式>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「概要」节（・SPRITEDISPOSEALL，约 189 行）；`EmueraEE_changelog.txt:81`「SPRITEDISPOSEALL追加」。ecd 文档只收录了单个精灵的 `SPRITEDISPOSE`（Command.md:2511），未收录本命令；zh 套件未收录。

## 语义

一次性销毁全部精灵（SPRITE）。参数为 0 时只销毁 ERB 中用 `SPRITECREATE` 等创建的精灵；参数非 0 时连 `resources` 文件夹内 CSV 定义的资源精灵也一并销毁。返回值为被销毁的精灵个数。

## 用法

### `SPRITEDISPOSEALL <是否连资源精灵一起销毁>`
- `<数值表达式>`：0 = 仅 ERB 创建的精灵；非 0 = 含 resources CSV 定义的精灵全部销毁。
- 返回值：本次被销毁的精灵数量。
```erb
;清掉 ERB 里创建的精灵，保留 resources 里定义的
N = SPRITEDISPOSEALL(0)
PRINTL 销毁了 {N} 个精灵
;全部清空
N = SPRITEDISPOSEALL(1)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:330`（`["SPRITEDISPOSEALL"] = new SpriteDisposeAllMethod()`，注册在 methodList，即**式中函数**）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6154`（`SpriteDisposeAllMethod`）；核心逻辑在 `UI/Game/Image/AppContents.cs:65`（`AppContents.SpriteDisposeAll`）

```text
SpriteDisposeAllMethod:
    构造:
        ReturnType = long
        argumentTypeArray = [long]
        CanRestructure = false
    GetIntValue(exm, arguments):
        返回 AppContents.SpriteDisposeAll(arguments[0].GetIntValue(exm) != 0)

AppContents.SpriteDisposeAll(delCsvImage):
    sprites  = imageDictionary.Count          # ERB 创建的精灵数
    csprites = resourceImageDictionary.Count  # resources CSV 定义的精灵数
    若 delCsvImage:
        imageDictionary.Clear()
        resourceImageDictionary.Clear()
        返回 sprites                          # 全部销毁，返回总数
    否则:
        imageDictionary = new ConcurrentDictionary(resourceImageDictionary)
                                              # 用资源精灵字典重建 → ERB 精灵全部消失，
                                              # 资源精灵保留（可再创建）
        返回 sprites - csprites               # 实际销毁的 ERB 精灵数
```

## 备注

- 与 `SPRITEDISPOSE <精灵名>`（单个销毁）相对，本命令是批量版；参数语义与 readme 完全一致（0 = 仅 ERB 创建的，非 0 = 含 CSV 定义的），返回值为销毁数。
- 文档说「命令」，本仓库实现为式中函数，需以 `SPRITEDISPOSEALL(0)` 形式使用（EM+EE 发行版 exe 中同样有此函数形态）。
- 销毁后不再对已显示的按钮/图像做额外清理，通常在场景切换时调用。
