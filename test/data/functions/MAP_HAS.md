# MAP_HAS

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：`int MAP_HAS(str 名称, str 键)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:234-237` 有记载（「◆ int MAP_HAS str, str 第一引数で指定した連想配列(Dictionary<string, string>)に，第二引数のキーの存否をチェックします 存在しているなら1を返す，そうでない場合0を返す 連想配列自体が存在しない場合，-1を返す」），`Emuera.EM_changelog.txt:23`（v3）追加。语义以源码为准。

## 语义

检查指定 map 中是否存在某个键。

- map 存在且含该键 → 返回 `1`。
- map 存在但不含该键 → 返回 `0`。
- map 本身不存在 → 返回 `-1`。

`MAP_GET` 在「键不存在」和「map 不存在」两种情况下都只返回空串、无法区分，readme 因此建议需要区分时先用本函数确认（readme 原文：例外発生しないので必要があれば MAP_HAS で確認してください）。注意本函数自身也用 `-1` 区分「map 不存在」，用 `0` 表示「map 在但键不在」。

## 用法

### int MAP_HAS(名称, 键)
- 名称：map 的名字（字符串）。
- 键：要查的键（字符串）。
- 返回值：含该键 `1`；不含 `0`；map 不存在 `-1`。
```erb
MAP_CREATE "ITEM"
MAP_SET "ITEM", "药水", "50"

PRINTL MAP_HAS("ITEM", "药水")     ; 1
PRINTL MAP_HAS("ITEM", "解毒药")   ; 0
PRINTL MAP_HAS("NOPE", "任意")     ; -1（map 不存在）
```

```erb
; 更细的判定：先区分「map 不存在」与「键不存在」
H = MAP_HAS("ITEM", "药水")
IF H < 0
    PRINTL "map 尚未建立"
ELSEIF H == 0
    PRINTL "map 在，但没有这个键"
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:272`（`["MAP_HAS"] = new MapDataOperationMethod(MapDataOperationMethod.Operation.Has)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1806`（`MapDataOperationMethod`；`Has` 的参数表在 `:1816-1818`、逻辑在 `:1836-1838`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`DataStringMaps`）

```text
MapDataOperationMethod（op = Has）:
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
        若 !dict.ContainsKey(map): 返回 -1        # map 不存在
        sMap = dict[map]
        若 op == Clear: sMap.Clear(); 返回 1
        若 op == Size:  返回 sMap.Count
        key = arguments[1] 的字符串值
        contains = sMap.ContainsKey(key)
        若 op == Has: 返回 contains ? 1 : 0        # 本函数
        若 op == Remove: sMap.Remove(key)
        否则（Set）: sMap[key] = arguments[2] 的字符串值
        返回 1
```

## 备注

- 语义据源码；EM readme 的「有 1 / 无 0 / map 不存在 -1」与源码一致，包括 readme 里那句「如需确认请用 MAP_HAS」的用意——本函数是唯一能区分「键不存在」与「map 不存在」的读取类函数（`MAP_GET` 两者都返回空串）。
- 键的比较是 `Dictionary` 的默认字符串比较：区分大小写（`Ordinal`），不受 `Config.IgnoreCase` 影响。
- 判断「值是否为空串」不能代替本函数：`MAP_SET "M","k",""` 之后 `MAP_GET` 与「键不存在」都返回 `""`，只有 `MAP_HAS` 能区分。
