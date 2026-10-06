# MAP_RELEASE

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：`int MAP_RELEASE(str 名称)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:226-227` 有记载（「◆ int MAP_RELEASE str 第一引数で指定した連想配列(Dictionary<string, string>)を削除します。1を返す」），`Emuera.EM_changelog.txt:23`（v3）追加。语义以源码为准。

## 语义

删除指定名称的 map（字符串关联数组）本身：连同其中所有键值一起从变量数据中移除（`DataStringMaps.Remove`）。

**返回值恒为 `1`**，无论该名称的 map 是否存在（源码 `if (contains) dict.Remove(key); return 1;`）。因此「是否真的删掉了」无法从返回值判断，需要先用 `MAP_EXIST` 确认。

它删除的是**整个 map**；只想清空内容而保留 map 本身请用 `MAP_CLEAR`。删除后该名称可以重新 `MAP_CREATE` 得到全新的空 map。

## 用法

### int MAP_RELEASE(名称)
- 名称：map 的名字（字符串）。
- 返回值：恒 `1`。
```erb
IF MAP_EXIST("TEMP")
    MAP_RELEASE "TEMP"      ; 整个 map 销毁
ENDIF
PRINTL MAP_EXIST("TEMP")    ; 0

; 对照：只清空内容、保留 map
MAP_CREATE "KEEP"
MAP_SET "KEEP", "a", "1"
MAP_CLEAR "KEEP"            ; 内容清空，MAP_EXIST 仍为 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:267`（`["MAP_RELEASE"] = new MapManagementMethod(MapManagementMethod.Operation.Release)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1780`（`MapManagementMethod`，`Release` 分支在 `:1799`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`DataStringMaps`）

```text
MapManagementMethod（op = Release）:
    构造:
        ReturnType = long
        argumentTypeArray = [typeof(string)]        # 恰好 1 个字符串参数
        CanRestructure = false
    GetIntValue(exm, arguments):
        key = arguments[0] 的字符串值
        dict = exm.VEvaluator.VariableData.DataStringMaps
        contains = dict.ContainsKey(key)
        switch (op):
            若 Check: 返回 contains ? 1 : 0
            若 Release:
                若 contains: dict.Remove(key)       # 连同全部键值删除
                返回 1                              # 恒为 1（不存在时也返回 1）
        # 落到这里的只有 Create
        若 contains: 返回 0
        dict[key] = new Dictionary<string, string>()
        返回 1
```

## 备注

- 语义据源码；EM readme 只说「删除并返回 1」，与源码一致。readme 未说明「不存在时也返回 1」，这是读了源码才确定的细节（如实记录，不作为推定）。
- 若该名称的 map 已由 `VarExt*.csv` 声明为随存档保存（`SAVE_MAPS`/`GLOBAL_MAPS`/`STATIC_MAPS`），删除后相应的存档项也会随之消失（保存时按 `ConstantData.SaveMaps` 等集合逐个查找 `DataStringMaps`，找不到就跳过，见 `Runtime/Script/Statements/Variable/VariableData.cs:1023-1035`）。
- 与 `DT_RELEASE`／`XML_RELEASE` 的对照：`XML_RELEASE` 在对象不存在时返回 `0`（`Runtime/Script/Statements/Function/Creator.Method.cs:786`），而 `MAP_RELEASE` 恒返回 `1`——两者的失败返回约定不一致。
