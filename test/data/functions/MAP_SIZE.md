# MAP_SIZE

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：`int MAP_SIZE(str 名称)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:248-250` 有记载（「◆ int MAP_SIZE str 第一引数で指定した連想配列(Dictionary<string, string>)に含まったキー-値ペアを返す 連想配列自体が存在しない場合，-1を返す」），`Emuera.EM_changelog.txt:18`（v4）追加。语义以源码为准。

## 语义

返回指定 map 中键值对的个数（`Dictionary.Count`）。

- map 存在 → 返回其键值对个数（空 map 返回 `0`）。
- 指定名称的 map 不存在 → 返回 `-1`。

注意「不存在（`-1`）」与「存在但为空（`0`）」是两种不同状态，需要区分时用 `MAP_EXIST` 确认存在性。清空后的 map 仍存在，故为 `0`。

## 用法

### int MAP_SIZE(名称)
- 名称：map 的名字（字符串）。
- 返回值：键值对个数；map 不存在 `-1`。
```erb
MAP_CREATE "SCORE"
PRINTL MAP_SIZE("SCORE")     ; 0

MAP_SET "SCORE", "Alice", "100"
MAP_SET "SCORE", "Bob", "80"
PRINTL MAP_SIZE("SCORE")     ; 2

PRINTL MAP_SIZE("NOPE")      ; -1
```

```erb
; 遍历全部键值：先取键，再逐个取值
MAP_GETKEYS "SCORE", 1
N = RESULT
FOR I, 0, N
    K = RESULTS:I
    PRINTFORML {K} = {MAP_GET("SCORE", K)}
NEXT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:271`（`["MAP_SIZE"] = new MapDataOperationMethod(MapDataOperationMethod.Operation.Size)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1806`（`MapDataOperationMethod`；`Size` 走默认参数表 `[typeof(string)]`、逻辑在 `:1828` 与 `:1833`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`DataStringMaps`）

```text
MapDataOperationMethod（op = Size）:
    构造:
        ReturnType = long
        argumentTypeArray = [typeof(string)]        # 恰好 1 个字符串参数
        CanRestructure = false
    GetIntValue(exm, arguments):
        map = arguments[0] 的字符串值
        dict = exm.VEvaluator.VariableData.DataStringMaps
        若 !dict.ContainsKey(map): 返回 -1
        sMap = dict[map]
        若 op == Clear: sMap.Clear(); 返回 1
        若 op == Size:  返回 sMap.Count            # 本函数
        （Has/Remove/Set 分支略，见 MAP_HAS/MAP_REMOVE/MAP_SET）
```

## 备注

- 语义据源码；EM readme 的「返回键值对个数、map 不存在返回 -1」与源码一致。
- `Count` 是 32 位 `int`，返回时隐式转成 `long`，无溢出风险。
- 与 `MAP_GETKEYS` 的关系：`MAP_GETKEYS` 也会把键的个数写入 `RESULT`（`RESULT:0`），因此若已经用 `MAP_GETKEYS` 取过键，可不必再调 `MAP_SIZE`（但 `MAP_GETKEYS` 的单参数形式**不**写 `RESULT`，见 `MAP_GETKEYS.md`）。
