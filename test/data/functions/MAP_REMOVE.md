# MAP_REMOVE

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：`int MAP_REMOVE(str 名称, str 键)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:244-246` 有记载（「◆ int MAP_REMOVE str, str 第一引数で指定した連想配列に，第二引数のキーとペアした値を削除する。1を返す。連想配列自体が存在しない場合，-1を返す」），`Emuera.EM_changelog.txt:23`（v3）追加。语义以源码为准。

## 语义

删除指定 map 中某个键（连同其值）。

- map 存在 → 返回 `1`；**键不存在时也返回 `1`**（源码 `sMap.Remove(key)` 的布尔返回值被忽略），因此「是否真的删掉了」无法从返回值判断，需要先用 `MAP_HAS` 确认。
- 指定名称的 map 不存在 → 返回 `-1`。

删除后 `MAP_SIZE` 减 1；键可以随后用 `MAP_SET` 重新建立。

## 用法

### int MAP_REMOVE(名称, 键)
- 名称：map 的名字（字符串）。
- 键：要删除的键（字符串）。
- 返回值：map 存在 `1`（键不存在也是 `1`）；map 不存在 `-1`。
```erb
MAP_CREATE "ITEM"
MAP_SET "ITEM", "药水", "50"
PRINTL MAP_SIZE("ITEM")               ; 1

PRINTL MAP_REMOVE("ITEM", "药水")      ; 1
PRINTL MAP_SIZE("ITEM")               ; 0

PRINTL MAP_REMOVE("ITEM", "不存在的键")  ; 1（键不存在也返回 1）
PRINTL MAP_REMOVE("NOPE", "任意")       ; -1（map 不存在）
```

```erb
; 需要知道到底删没删，就先查
IF MAP_HAS("ITEM", "药水") == 1
    MAP_REMOVE "ITEM", "药水"
    PRINTL "已删除"
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:274`（`["MAP_REMOVE"] = new MapDataOperationMethod(MapDataOperationMethod.Operation.Remove)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1806`（`MapDataOperationMethod`；`Remove` 的参数表在 `:1816-1818`、逻辑在 `:1836-1843`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`DataStringMaps`）

```text
MapDataOperationMethod（op = Remove）:
    构造:
        ReturnType = long
        switch (type):
            Set: [string, string, string]
            Has/Remove: [string, string]        # 本函数用 2 个参数
            default（Clear/Size）: [string]
        CanRestructure = false
    GetIntValue(exm, arguments):
        map = arguments[0] 的字符串值
        dict = exm.VEvaluator.VariableData.DataStringMaps
        若 !dict.ContainsKey(map): 返回 -1
        sMap = dict[map]
        若 op == Clear: sMap.Clear(); 返回 1
        若 op == Size:  返回 sMap.Count
        key = arguments[1] 的字符串值
        contains = sMap.ContainsKey(key)
        若 op == Has: 返回 contains ? 1 : 0
        若 op == Remove: sMap.Remove(key)       # 返回值被丢弃
        否则: sMap[key] = arguments[2] 的字符串值
        返回 1                                   # 恒为 1
```

## 备注

- 语义据源码；EM readme 的「删除并返回 1、map 不存在返回 -1」与源码一致。readme 未说明「键不存在时也返回 1」，此为源码细节（`Dictionary.Remove` 的 `bool` 结果未被使用），如实记录。
- 键的比较是 `Dictionary` 默认的区分大小写比较（`Ordinal`），不受 `Config.IgnoreCase` 影响。
- 与 `MAP_CLEAR`（清掉全部键值但保留 map）、`MAP_RELEASE`（连 map 一起删）的分工不同；三者都返回非 `-1` 的「成功」值，其中 `MAP_RELEASE` 恒返回 `1`。
