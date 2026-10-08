# ISDEFINED

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（EM 扩展）
- **签名**：`int ISDEFINED(str 名称)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:30-31`（「◆ int ISDEFINED str」）有记载。语义以源码为准。

## 语义

判断给定名称是否作为**宏**（`#DEFINE` 定义的名字）存在：存在返回 `1`，不存在返回 `0`。它相当于「宏版的存在检查」，配合 `#IF` 之类预处理可写出「这个宏有没有被定义」的分支。

查找走 `IdentifierDictionary.GetMacro`，是**大小写敏感度可配置**的：由 `Config.IgnoreCase`（配置项「大文字小文字を区別しない」）决定按 `OrdinalIgnoreCase` 还是 `Ordinal` 比较。因此同一份脚本在不同配置下可能得到不同结果。

它只查宏（`#DEFINE`），不查变量与函数——变量请用 `EXISTVAR`，函数请用 `EXISTFUNCTION`。

## 用法

### int ISDEFINED(名称)
- 名称：字符串表达式，待查的宏名。
- 返回值：宏已定义 → `1`；未定义 → `0`。
```erb
#DEFINE DEBUG_MODE 1

@CHECK
IF ISDEFINED("DEBUG_MODE")
    PRINTL "调试模式已定义，其值为 " + TOSTR(DEBUG_MODE)
ELSE
    PRINTL "未定义 DEBUG_MODE"
ENDIF
```

```erb
; 反向用法：给可选的宏提供缺省值
#IF !ISDEFINED("MAX_ITEM")
    #DEFINE MAX_ITEM 100
#ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:224`（`["ISDEFINED"] = new IsDefinedMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:137`（`IsDefinedMethod`）→ `Runtime/Script/Data/IdentifierDictionary.cs:468`（`GetMacro`），宏的登记在 `Runtime/Script/Data/IdentifierDictionary.cs:440`（`AddMacro`）

```text
IsDefinedMethod:
    构造:
        ReturnType = long
        argumentTypeArray = [string]        # 恰好 1 个参数
        CanRestructure = true               # 参数为常量时可在解析期折叠
    GetIntValue(exm, arguments):
        返回 (GlobalStatic.IdentifierDictionary.GetMacro(arguments[0] 的字符串值) != null) ? 1 : 0

IdentifierDictionary.GetMacro(key):
    若 Config.IgnoreCase:
        hash = key.GetHashCode(StringComparison.OrdinalIgnoreCase)
    否则:
        hash = key.GetHashCode(StringComparison.Ordinal)
    若 macroDic 以该 hash 为键存在: 返回对应 DefineMacro
    否则: 返回 null

IdentifierDictionary.AddMacro(mac)（:440，宏的登记侧，供对照）:
    nameDic.Add(mac.Keyword, DefinedNameType.UserMacro)
    key = Config.IgnoreCase ? mac.Keyword.GetHashCode(OrdinalIgnoreCase)
                            : mac.Keyword.GetHashCode(Ordinal)
    macroDic.Add(key, mac)
```

## 备注

- 语义据源码；EM readme 的描述（「与给定字符串同名的宏 `#DEFINE XXX` 已定义则返回 1，否则 0」）与源码一致。
- **源码缺陷（推定为碰撞风险）**：`macroDic` 的键是**字符串的哈希值**而非字符串本身（`Runtime/Script/Data/IdentifierDictionary.cs:429`），`GetMacro` 也只比哈希。哈希不同的名字当然不会误命中，但理论上不同的宏名若哈希相同会被当成同一个宏（`AddMacro` 甚至会因键重复而覆盖）。这是该实现固有的碰撞面，实际游戏规模下几乎不会触发，标注为推定。
- 大小写：`Config.IgnoreCase` 开启时 `ISDEFINED("foo")` 能查到 `#DEFINE FOO`；关闭时不能。
- `CanRestructure = true` 意味着参数为字面量时会退化为常量：宏的「定义与否」在解析期就已确定，运行时不再变化，这不影响正确性。
- 与同族的对照：`EXISTVAR`（判断变量，返回按位标记，EM readme:32-62）与 `EXISTFUNCTION`（判断函数，EE 扩展）在本仓库均已注册；`ISDEFINED` 只覆盖宏。
