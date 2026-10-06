# MAP_GET

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：`str MAP_GET(str 名称, str 键)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:229-232` 有记载（「◆ str MAP_GET str, str 第一引数で指定した連想配列に，第二引数のキーが存在する場合，その値を返す。存在していない場合，または連想配列自体が存在しない場合も，空文字列を返す (例外発生しないので必要があればMAP_HASで確認してください)」），`Emuera.EM_changelog.txt:23`（v3）追加。语义以源码为准。

## 语义

读取指定 map 中某个键对应的值。

- map 存在且含该键 → 返回其值。
- map 存在但不含该键 → 返回空串 `""`。
- map 本身不存在 → 也是空串 `""`（**不报错、不返回 -1**）。

因为三种「空串」无法互相区分，需要判断「键到底在不在」时要另用 `MAP_HAS`（返回值 `1`/`0`/`-1`）。同样地，「值为空串」与「键不存在」也只能靠 `MAP_HAS` 区分。

## 用法

### str MAP_GET(名称, 键)
- 名称：map 的名字（字符串）。
- 键：要读取的键（字符串）。
- 返回值：对应的值；键或 map 不存在时为空串。
```erb
MAP_CREATE "ITEM"
MAP_SET "ITEM", "药水", "50"

PRINTL MAP_GET("ITEM", "药水")     ; 50
PRINTL MAP_GET("ITEM", "解毒药")   ; （空行）
PRINTL MAP_GET("NOPE", "任意")     ; （空行）
```

```erb
; 带缺省值的安全读取
FUNC GET_OR, NAME, KEY, DEFAULT
    IF MAP_HAS(NAME, KEY) == 1
        RETURNF MAP_GET(NAME, KEY)
    ENDIF
    RETURNF DEFAULT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:269`（`["MAP_GET"] = new MapGetStrMethod(MapGetStrMethod.Operation.Get)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1847`（`MapGetStrMethod`；`Get` 的参数表在 `:1855-1856`、逻辑在 `:1869-1880`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`DataStringMaps`）

```text
MapGetStrMethod（op = Get）:
    构造:
        ReturnType = string
        switch (type):
            Get:    argumentTypeArray = [typeof(string), typeof(string)]      # 恰好 2 个参数
            ToXml:  argumentTypeArray = [typeof(string)]                      # 恰好 1 个参数
            GetKeys: argumentTypeArrayEx = [
                        { ArgTypes = { String, Int }, OmitStart = 1 },        # 1~2 个参数
                        { ArgTypes = { String, RefString1D, Int } } ]         # 恰好 3 个参数
        CanRestructure = false
    GetStrValue(exm, arguments):
        dict = exm.VEvaluator.VariableData.DataStringMaps
        map = arguments[0] 的字符串值
        若 !dict.ContainsKey(map): 返回 ""          # map 不存在 → 空串（不报错）
        sMap = dict[map]
        若 op == Get:
            key = arguments[1] 的字符串值
            若 sMap.ContainsKey(key): 返回 sMap[key]
            返回 ""
        （GetKeys / ToXml 分支见各自文档）
```

## 备注

- 语义据源码；EM readme 的「键存在返回值、否则空串（map 不存在也是空串，需要时用 MAP_HAS 确认）」与源码逐字对应。
- 其余 map 函数在「map 不存在」时返回 `-1`，只有本函数返回空串——这是刻意设计（读取不报错），但会让「值是空串」与「键不存在」不可区分。`MAP_HAS` 是唯一的区分手段（`MAP_GETKEYS`/`MAP_TOXML` 在 map 不存在时也返回空串）。
- 键的比较是 `Dictionary` 默认的区分大小写比较（`Ordinal`），不受 `Config.IgnoreCase` 影响。
- 参数个数固定为 2，类型固定为两个字符串；`MAP_GET(MAP_NO)` 之类会在解析期报错。
