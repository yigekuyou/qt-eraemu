# XML_REMOVEATTRIBUTE_BYNAME

- **类别**：式中函数（EM 扩展 / XML 文档表，`XML_REMOVEATTRIBUTE` 的按名变体）
- **签名**：`int XML_REMOVEATTRIBUTE_BYNAME(str 文档名, str xpath{, int 全部删除})`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）、EE readme、本分支 EM readme 均未收录 `_BYNAME` 变体（EM readme 只写了 `XML_REMOVEATTRIBUTE`，`Emuera.EM_readme.txt:208`）。语义据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:1048` 的 `XmlRemoveNodeMethod`，注册时传 `byname: true` 与 `Operation.Attribute`），属无外部文档、纯按源码编写的条目。

## 语义

与 `XML_REMOVEATTRIBUTE` 同一实现，差别有两处：

1. 参数表只剩一种形态：`String, String, Int`（`OmitStart = 2`，即 2~3 个参数）——**第 1 参数只能是字符串**，且一律被当作**文档表的名字**。
2. 没有「回写变量」的行为：删除只发生在文档表里，用 `XML_TOSTR(文档名)` 取回结果。

其余语义与 `XML_REMOVEATTRIBUTE` 相同：XPath 必须选出属性节点（`@` 轴），选中元素则单匹配返回 `0`、多匹配被跳过；多属性需第 3 参数非 `0` 才删；文档不存在返回 `-1`；XPath/XML 错误抛 `CodeEE`。

## 用法

### int XML_REMOVEATTRIBUTE_BYNAME(文档名, xpath{, 全部删除})
- 文档名：文档表里的名字（字符串）。
- xpath：选出属性节点的 XPath（如 `//a/@id`、`//@*`）。
- 全部删除：匹配多个属性时，`0`/省略 → 一个都不删；非 `0` → 全部删除。
- 返回值：匹配个数；单匹配删除失败 `0`；文档不存在 `-1`。
```erb
XML_DOCUMENT("save1", "<root><a id=""1"" x=""9"" y=""8"">t</a></root>")

; 多属性 + setall 省略 → 不删
PRINTL XML_REMOVEATTRIBUTE_BYNAME("save1", "//a/@*")       ; 2
PRINTL XML_TOSTR("save1")                                    ; <root><a id="1" x="9" y="8">t</a></root>

XML_REMOVEATTRIBUTE_BYNAME "save1", "//a/@*", 1
PRINTL XML_TOSTR("save1")                                    ; <root><a>t</a></root>

PRINTL XML_REMOVEATTRIBUTE_BYNAME("nope", "//@x")            ; -1（文档不存在）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:263`（`["XML_REMOVEATTRIBUTE_BYNAME"] = new XmlRemoveNodeMethod(XmlRemoveNodeMethod.Operation.Attribute, true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1048`（`XmlRemoveNodeMethod`；`byName` 在 `:1068`，构造覆写在 `:1061`，`Remove` 的 `Attribute` 分支在 `:1072-1079`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlRemoveNodeMethod（op = Attribute, byname = true 的构造）:
    构造(op, byname):
        ReturnType = long
        byName = byname
        argumentTypeArrayEx = [
            { ArgTypes = { String, String, Int }, OmitStart = 2 }   # ★ 只剩字符串形态，2~3 个参数
        ]
        CanRestructure = false

    Remove(node):                       # :1070
        若 op == Attribute:
            若 node 是 XmlAttribute: attr.OwnerElement.Attributes.Remove(attr); 返回 true
            返回 false                   # 非属性节点 → 失败
        （Node 分支见 XML_REMOVENODE）

    GetIntValue(exm, arguments):
        method = (参数个数 >= 4) ? arguments[3] : 0     # 死代码，恒 0 且未被使用
        saveToArg0 = true
        若 arguments[0] 是整数 或 (byName 且 arguments[0] 是字符串):     # ★ 字符串一律当文档名
            saveToArg0 = false
            idx = arguments[0] 的字符串值；文档表含 idx ? doc = 表[idx] : 返回 -1
        否则: （本变体不可达）
        path = arguments[1]；SelectNodes 失败 → CodeEE(XmlXPathParseError, ...)
        若 nodes.Count > 0:
            setAllNodes = (参数个数 == 3) ? arguments[2] != 0 : false
            若 nodes.Count != 1: 若 setAllNodes: 逐个 Remove
            否则: 若 !Remove(nodes[0]): 返回 0
            若 saveToArg0: 回写（本变体恒 false，不执行）
        返回 nodes.Count
```

## 备注

- **无外部文档**：`XML_REMOVEATTRIBUTE_BYNAME` 未出现在两套中文文档、EE readme、EM readme 中；本文据源码编写。与 `XML_REMOVEATTRIBUTE` 的差异即「字符串第 1 参数 = 文档名」与「没有 `RefString` 形态」两点。
- XPath 需用 `@` 轴选属性；选中元素节点时删除失败（单匹配返回 `0`，多匹配静默跳过）。
- 文档名区分大小写（`Dictionary` 默认 `Ordinal` 比较）。
- 多属性 + `全部删除 = 0` 时返回匹配数但一个都不删。
- 字符串形态不支持整数 ID，需要整数 ID 请用 `XML_REMOVEATTRIBUTE`。
