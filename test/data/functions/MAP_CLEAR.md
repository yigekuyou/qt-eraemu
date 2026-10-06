# MAP_CLEAR

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：`int MAP_CLEAR(str 名称)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:252-254` 有记载（「◆ int MAP_CLEAR str 第一引数で指定した連想配列(Dictionary<string, string>)に含まったキー-値ペアを全部削除する。1を返す 連想配列自体が存在しない場合，-1を返す」），`Emuera.EM_changelog.txt:18`（v4）追加。语义以源码为准。

## 语义

删除指定 map 中的**全部键值对**（`sMap.Clear()`），但保留 map 自身——清空后 `MAP_EXIST` 仍为 `1`、`MAP_SIZE` 为 `0`。要连 map 一起销毁请用 `MAP_RELEASE`。

- 清空成功 → 返回 `1`。
- 指定名称的 map 不存在 → 返回 `-1`（**不报错**）。

## 用法

### int MAP_CLEAR(名称)
- 名称：map 的名字（字符串）。
- 返回值：成功 `1`；map 不存在 `-1`。
```erb
MAP_CREATE "TEMP"
MAP_SET "TEMP", "a", "1"
MAP_SET "TEMP", "b", "2"
PRINTL MAP_SIZE("TEMP")      ; 2

MAP_CLEAR "TEMP"
PRINTL MAP_SIZE("TEMP")      ; 0
PRINTL MAP_EXIST("TEMP")     ; 1（map 本身还在）

PRINTL MAP_CLEAR("NOPE")     ; -1（不存在的 map）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:270`（`["MAP_CLEAR"] = new MapDataOperationMethod(MapDataOperationMethod.Operation.Clear)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1806`（`MapDataOperationMethod`；`Clear` 走默认参数表 `[typeof(string)]`、逻辑在 `:1828-1832`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`DataStringMaps`）

```text
MapDataOperationMethod（op = Clear）:
    构造:
        ReturnType = long
        switch (type):
            Set:    argumentTypeArray = [string, string, string]     # 3 个参数
            Has/Remove: argumentTypeArray = [string, string]         # 2 个参数
            default（含 Clear/Size）: argumentTypeArray = [string]    # 1 个参数
        CanRestructure = false
    GetIntValue(exm, arguments):
        map = arguments[0] 的字符串值
        dict = exm.VEvaluator.VariableData.DataStringMaps
        若 !dict.ContainsKey(map): 返回 -1          # map 不存在（Clear/Size/Has/Remove/Set 统一行为）
        sMap = dict[map]
        若 op == Clear: sMap.Clear()                # 本函数
        若 op == Size:  返回 sMap.Count
        否则（Has/Remove/Set）:
            key = arguments[1] 的字符串值
            contains = sMap.ContainsKey(key)
            若 Has: 返回 contains ? 1 : 0
            若 Remove: sMap.Remove(key)
            否则: sMap[key] = arguments[2] 的字符串值
        返回 1
```

## 备注

- 语义据源码；EM readme 的「全部删除返回 1、map 不存在返回 -1」与源码一致。
- 与 `MAP_RELEASE` 的对照：本函数保留 map 的「存在性」，`MAP_RELEASE` 销毁它。两者都会让 `MAP_SIZE` 变成「不存在」或 `0`，下游代码需区分这两种状态时用 `MAP_EXIST`。
- 返回值 `-1` 是这一族函数表示「map 不存在」的通用约定（`MAP_GET` 例外——它返回空串），与 `XML_*` 族的 `-1` 约定相同。
- 参数个数固定为 1（`[typeof(string)]`，无 `OmitStart`），多给参数会在解析期报错。
