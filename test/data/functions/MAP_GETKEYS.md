# MAP_GETKEYS

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：
  - `str MAP_GETKEYS(str 名称)`
  - `str MAP_GETKEYS(str 名称, int 输出到 RESULTS)`
  - `str MAP_GETKEYS(str 名称, strArray 输出数组, int 输出到数组)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:256-264` 有记载（「◆ <1> str MAP_GETKEYS str ／ <2> str MAP_GETKEYS str, int ／ <3> str MAP_GETKEYS str, strArray, int」），`Emuera.EM_changelog.txt:18`（v4）追加。语义以源码为准。

## 语义

取出指定 map 中的全部键。

- **形式 1**：返回把全部键用半角逗号拼接成的字符串（如 `"キー1,キー2,キー3"`）。**不**写 `RESULT`，也**不**写 `RESULTS`。
- **形式 2**：第 2 参数（整数）非 `0` 时，把各键依次写入 `RESULTS:0`、`RESULTS:1`……，并把键的**个数**写入 `RESULT`（即 `RESULT:0`）；返回值是 `RESULTS:0`，也就是第一个键。第 2 参数为 `0` 时直接返回空串。
- **形式 3**：第 3 参数（整数）非 `0` 时，把各键依次写入第 2 参数指定的字符串数组变量（**从头覆盖，忽略变量上写的下标**），并把键的个数写入 `RESULT`；返回值恒为空串。第 3 参数为 `0` 时直接返回空串。

三种形式在 map 不存在时都返回空串。数组/RESULTS 放不下的键被静默丢弃（写入上限为数组长度）。

键的迭代顺序是 `Dictionary` 的内部顺序（即插入顺序在无删除时的实际顺序，但源码层面无任何顺序保证）；`MAP_REMOVE` 后重新 `MAP_SET` 可能导致顺序变化。需要有序时应自行排序。

## 用法

### str MAP_GETKEYS(名称)
```erb
MAP_CREATE "ITEM"
MAP_SET "ITEM", "药水", "50"
MAP_SET "ITEM", "解毒药", "30"
PRINTL MAP_GETKEYS("ITEM")     ; 药水,解毒药（顺序依字典内部顺序）
```

### str MAP_GETKEYS(名称, 输出到 RESULTS)
- 第 2 参数非 `0` → 写 `RESULTS` 与 `RESULT`；为 `0` → 返回空串。
- 返回值：`RESULTS:0`（第一个键）。
```erb
MAP_GETKEYS "ITEM", 1
PRINTFORML 共 {RESULT} 个键；第一个是 {RESULTS}
FOR I, 0, RESULT
    PRINTFORML 第{I}个：{RESULTS:I}
NEXT
```

### str MAP_GETKEYS(名称, 输出数组, 输出到数组)
- 第 2 参数：一维字符串数组变量；第 3 参数非 `0` 才输出。
- 返回值：恒空串。
```erb
#DIMS KEYS = 64
MAP_GETKEYS "ITEM", KEYS, 1
PRINTFORML 共 {RESULT} 个键
FOR I, 0, RESULT
    PRINTFORML KEYS:{I} = {KEYS:I}
NEXT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:275`（`["MAP_GETKEYS"] = new MapGetStrMethod(MapGetStrMethod.Operation.GetKeys)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1847`（`MapGetStrMethod`；参数表在 `:1859-1863`、`GetKeys` 逻辑在 `:1881-1919`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`DataStringMaps`）

```text
MapGetStrMethod（op = GetKeys）:
    构造:
        ReturnType = string
        argumentTypeArrayEx = [
            { ArgTypes = { String, Int }, OmitStart = 1 },        # 1~2 个参数（形式 1、2）
            { ArgTypes = { String, RefString1D, Int } }           # 恰好 3 个参数（形式 3）
        ]
        CanRestructure = false
    GetStrValue(exm, arguments):
        dict = exm.VEvaluator.VariableData.DataStringMaps
        map = arguments[0] 的字符串值
        若 !dict.ContainsKey(map): 返回 ""            # map 不存在 → 空串
        sMap = dict[map]

        若 op == GetKeys 且 参数个数 > 1:
            count = 0
            若 参数个数 == 3:                          # 形式 3：输出到字符串数组
                Term = arguments[1] as VariableTerm
                若 arguments[2] 的值 == 0: 返回 ""
                array = Term.Identifier.GetArray() as string[]     # 整个数组，忽略变量上的下标
            否则若 参数个数 == 2:                      # 形式 2：输出到 RESULTS
                若 arguments[1] 的值 == 0: 返回 ""
                array = exm.VEvaluator.RESULTS_ARRAY
            否则:
                返回 ""
            遍历 sMap.Keys 中的 k:
                若 count >= array.Length: 跳出        # 放不下就丢弃剩余键
                array[count] = k
                count++
            exm.VEvaluator.RESULT = sMap.Keys.Count     # RESULT:0 ← 键个数
            返回 (参数个数 == 2) ? exm.VEvaluator.RESULTS : ""
                                                       # 形式 2 返回 RESULTS:0；形式 3 返回 ""
        # 形式 1（参数个数 == 1）：拼逗号串
        sb = StringBuilder；isNotEmpty = false
        若 op == GetKeys:
            遍历 sMap.Keys 中的 k:
                若 isNotEmpty: sb.Append(",").Append(k)
                否则: isNotEmpty = true; sb.Append(k)
            # 注意：此分支不写 RESULT
        否则（ToXml）: 见 MAP_TOXML
        返回 sb.ToString()
```

## 备注

- 语义据源码；EM readme 的三种形式与「map 不存在返回空串」「键的个数写入 RESULT」与源码一致，但 readme 把「键的个数写入 RESULT」写成了无条件的说明——实际**只有形式 2、3 会写**，形式 1 不写（源码 `:1903` 在 `参数个数 > 1` 的分支内），如实记录。
- 形式 2 的返回值是 `RESULTS:0`（第一个键），不是空串也不是个数；`RESULTS` 与 `RESULT` 都被本次调用改写。
- 形式 3 会**覆盖目标数组的前 N 个元素**（N = min(键数, 数组长度)），不清理其余元素，也不看变量名里写的下标；剩余旧值仍在数组里。
- 形式 3 的第 2 参数必须是**一维字符串数组变量**（`RefString1D`），传标量变量或字面量会在解析期报错；形式 2 的第 2 参数必须是整数。
- 键的迭代顺序即 `Dictionary.Keys` 的顺序（源码不做排序）；`MAP_TOXML` 用的也是同一顺序。
- 形式 1 在 map 存在但为空时返回空串（`StringBuilder` 为空），与「map 不存在」返回的空串不可区分（可用 `MAP_EXIST` 区分）。
