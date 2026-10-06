# MAP_SET

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：`int MAP_SET(str 名称, str 键, str 值)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:239-242` 有记载（「◆ int MAP_SET str, str, str 第一引数で指定した連想配列に，第二引数のキーとペアした値に第三引数を代入 キーが存在していない場合キーを作る。1を返す。連想配列自体が存在しない場合，-1を返す」），`Emuera.EM_changelog.txt:23`（v3）追加。语义以源码为准。

## 语义

向指定 map 写入一个键值对：键存在则覆盖原值，键不存在则新建。map 必须先用 `MAP_CREATE` 建立。

- 写入成功（新建或覆盖）→ 返回 `1`。
- 指定名称的 map 不存在 → 返回 `-1`，**不会自动创建 map**。

值必须是字符串（整数会自动按字符串形式参与？——不会：参数类型在解析期就要求字符串，整数表达式会报错，需自己用 `TOSTR` 转换）。

## 用法

### int MAP_SET(名称, 键, 值)
- 名称：map 的名字（字符串）。
- 键：键（字符串）。
- 值：值（字符串表达式）。
- 返回值：成功 `1`；map 不存在 `-1`。
```erb
MAP_CREATE "SCORE"
PRINTL MAP_SET("SCORE", "Alice", "100")       ; 1
PRINTL MAP_SET("SCORE", "Alice", "120")       ; 1（覆盖）
PRINTL MAP_GET("SCORE", "Alice")              ; 120
PRINTL MAP_SET("NOPE", "k", "v")              ; -1（map 不存在）
```

```erb
; 整数需要显式转字符串
MAP_SET "SCORE", "Bob", TOSTR(80)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:273`（`["MAP_SET"] = new MapDataOperationMethod(MapDataOperationMethod.Operation.Set)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1806`（`MapDataOperationMethod`；`Set` 的参数表在 `:1814-1815`、逻辑在 `:1836-1843`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`DataStringMaps`）

```text
MapDataOperationMethod（op = Set）:
    构造:
        ReturnType = long
        switch (type):
            Set: argumentTypeArray = [typeof(string), typeof(string), typeof(string)]   # 恰好 3 个参数
            Has/Remove: [typeof(string), typeof(string)]
            default（Clear/Size）: [typeof(string)]
        CanRestructure = false
    GetIntValue(exm, arguments):
        map = arguments[0] 的字符串值
        dict = exm.VEvaluator.VariableData.DataStringMaps
        若 !dict.ContainsKey(map): 返回 -1            # 不自动创建
        sMap = dict[map]
        若 op == Clear: sMap.Clear(); 返回 1
        若 op == Size:  返回 sMap.Count
        key = arguments[1] 的字符串值
        contains = sMap.ContainsKey(key)              # contains 在 Set 分支中未被使用
        若 op == Has: 返回 contains ? 1 : 0
        若 op == Remove: sMap.Remove(key)
        否则: sMap[key] = arguments[2] 的字符串值      # 键不存在则新建索引器条目
        返回 1
```

## 备注

- 语义据源码；EM readme 的「键不存在就创建、返回 1、map 不存在返回 -1」与源码一致。
- 键的比较是 `Dictionary` 默认的区分大小写比较（`Ordinal`）：`MAP_SET("M","abc","1")` 之后 `MAP_HAS("M","ABC")` 为 `0`，不受 `Config.IgnoreCase` 影响。
- 值以字符串存储，无类型信息；要当数字用需自己 `TOI`/`TOINT`（或用 `MAP_GET` 取出后转换）。
- 想「只在键不存在时写入」需要自己用 `MAP_HAS` 判断（没有 `MAP_SETDEFAULT` 之类）。
- 写入不受 `VarExt*.csv` 声明影响（声明只管存档时是否保存，不影响内存中的可写性）。
