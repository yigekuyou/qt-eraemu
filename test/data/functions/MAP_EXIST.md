# MAP_EXIST

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：`int MAP_EXIST(str 名称)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:222-224` 有记载（「◆ int MAP_EXIST str 第一引数で指定した連想配列(Dictionary<string, string>)の存否をチェックします 存在しているなら1を返す，そうでない場合0を返す」），`Emuera.EM_changelog.txt:23`（v3）追加。语义以源码为准。

## 语义

检查指定名称的 map（字符串关联数组）是否已由 `MAP_CREATE` 创建：存在返回 `1`，不存在返回 `0`。它只判断「map 本身是否存在」，不判断里面有没有键（判断键用 `MAP_HAS`）。

与 `MAP_GET`／`MAP_SET` 等的区别：后两者在 map 不存在时返回 `-1` 或空串，而本函数返回 `0`——因此**存在性检查请用本函数**，不要用 `MAP_HAS` 的 `-1` 去推断（虽然也能推出来）。

## 用法

### int MAP_EXIST(名称)
- 名称：map 的名字（字符串）。
- 返回值：存在 `1`；不存在 `0`。
```erb
IF MAP_EXIST("ITEM")
    PRINTL "map 存在，共 " + TOSTR(MAP_SIZE("ITEM")) + " 项"
ELSE
    MAP_CREATE "ITEM"
ENDIF
```

```erb
; 惰性初始化模式
IF !MAP_EXIST("FLAGS")
    MAP_CREATE "FLAGS"
ENDIF
MAP_SET "FLAGS", "初见", "1"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:266`（`["MAP_EXIST"] = new MapManagementMethod(MapManagementMethod.Operation.Check)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1780`（`MapManagementMethod`，`Check` 分支在 `:1798`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`DataStringMaps`）

```text
MapManagementMethod（op = Check）:
    构造:
        ReturnType = long
        argumentTypeArray = [typeof(string)]        # 恰好 1 个字符串参数
        CanRestructure = false
    GetIntValue(exm, arguments):
        key = arguments[0] 的字符串值
        dict = exm.VEvaluator.VariableData.DataStringMaps
        contains = dict.ContainsKey(key)
        switch (op):
            若 Check: 返回 contains ? 1 : 0          # 本函数
            若 Release: 若 contains 则 dict.Remove(key); 返回 1
        # Create 分支: 存在则 0，否则新建并返回 1
```

## 备注

- 语义据源码；EM readme 的「存在 1／不存在 0」与源码一致。
- 与 `MAP_RELEASE` 的对照：`MAP_RELEASE` **恒返回 1**（无论是否真的删掉了），所以「删之前先确认存在」只能靠本函数。
- map 的存在性与内容相互独立：`MAP_CLEAR` 之后 map 仍存在（本函数返回 1），`MAP_RELEASE` 之后才返回 0。
