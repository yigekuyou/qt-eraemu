# XML_REMOVENODE_BYNAME

- **类别**：式中函数（EM 扩展 / XML 文档表，`XML_REMOVENODE` 的按名变体）
- **签名**：`int XML_REMOVENODE_BYNAME(str 文档名, str xpath{, int 全部删除})`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）、EE readme、本分支 EM readme 均未收录 `_BYNAME` 变体（EM readme 只写了 `XML_REMOVENODE`，`Emuera.EM_readme.txt:185`）。语义据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:1048` 的 `XmlRemoveNodeMethod`，注册时传 `byname: true`），属无外部文档、纯按源码编写的条目。

## 语义

与 `XML_REMOVENODE` 同一实现，差别有两处：

1. 参数表只剩一种形态：`String, String, Int`（`OmitStart = 2`，即 2~3 个参数）——**第 1 参数只能是字符串**，且一律被当作**文档表的名字**（不是 XML 文本，也不是整数 ID）。
2. 因此没有「把结果回写变量」的行为：改写只发生在文档表里，用 `XML_TOSTR(文档名)` 取回结果。

其余语义与 `XML_REMOVENODE` 相同：删元素节点、根节点删不掉（返回 `0`）、多节点需第 3 参数非 `0` 才删、文档不存在返回 `-1`、XPath 错误抛 `CodeEE`。

## 用法

### int XML_REMOVENODE_BYNAME(文档名, xpath{, 全部删除})
- 文档名：文档表里的名字（字符串）。
- xpath：XPath 表达式。
- 全部删除：匹配多个节点时，`0`/省略 → 一个都不删；非 `0` → 全部删除。
- 返回值：匹配个数；单节点删除失败（根节点）`0`；文档不存在 `-1`。
```erb
XML_DOCUMENT("save1", "<root><keep/><drop/><drop/></root>")

; 多个匹配且 setall 省略 → 不删（但返回匹配数）
PRINTL XML_REMOVENODE_BYNAME("save1", "//drop")          ; 2
PRINTL XML_TOSTR("save1")                                 ; <root><keep /><drop /><drop /></root>

XML_REMOVENODE_BYNAME "save1", "//drop", 1
PRINTL XML_TOSTR("save1")                                 ; <root><keep /></root>

PRINTL XML_REMOVENODE_BYNAME("nope", "//a")               ; -1（文档不存在）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:257`（`["XML_REMOVENODE_BYNAME"] = new XmlRemoveNodeMethod(XmlRemoveNodeMethod.Operation.Node, true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1048`（`XmlRemoveNodeMethod`；`byName` 字段在 `:1068`，构造覆写在 `:1061`，`Remove` 在 `:1070`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlRemoveNodeMethod（op = Node, byname = true 的构造）:
    构造(op, byname):
        ReturnType = long
        byName = byname
        argumentTypeArrayEx = [
            { ArgTypes = { String, String, Int }, OmitStart = 2 }   # ★ 只剩字符串形态，2~3 个参数
        ]
        CanRestructure = false

    Remove(node):                       # :1070
        若 op == Attribute: 要求 node 是 XmlAttribute，从 owner 元素删除
        否则: 若 node.ParentNode != null: RemoveChild; 返回 true；否则返回 false   # 根节点 → false

    GetIntValue(exm, arguments):
        method = (参数个数 >= 4) ? arguments[3] : 0     # 死代码，恒 0 且未被使用
        saveToArg0 = true
        若 arguments[0] 是整数 或 (byName 且 arguments[0] 是字符串):     # ★ byName 让字符串一律当文档名
            saveToArg0 = false
            idx = arguments[0] 的字符串值；文档表含 idx ? doc = 表[idx] : 返回 -1
        否则: （本变体不可达）解析 XML 文本；失败 → CodeEE(XmlParseError, ...)
        path = arguments[1]；SelectNodes 失败 → CodeEE(XmlXPathParseError, ...)
        若 nodes.Count > 0:
            setAllNodes = (参数个数 == 3) ? arguments[2] != 0 : false
            若 nodes.Count != 1: 若 setAllNodes: 逐个 Remove
            否则: 若 !Remove(nodes[0]): 返回 0        # 单节点删除失败 → 0（且不回写）
            若 saveToArg0: 回写 arguments[0]（本变体恒 false，不执行）
        返回 nodes.Count
```

## 备注

- **无外部文档**：`XML_REMOVENODE_BYNAME` 未出现在两套中文文档、EE readme、EM readme 中；本文据源码编写。与 `XML_REMOVENODE` 的差异即「字符串第 1 参数 = 文档名」与「没有 `RefString` 形态」两点。
- 与 `XML_REMOVENODE` 的对照：`XML_REMOVENODE("doc", ...)` 会把 `"doc"` 当 XML 文本解析（通常抛解析错误），只有 `_BYNAME` 才按文档名查找。
- 文档名区分大小写（`Dictionary` 默认 `Ordinal` 比较）。
- 多节点 + `全部删除 = 0` 时返回匹配数但一个都不删；根节点匹配时返回 `0`。
- 字符串形态无法用整数 ID（参数表固定 `String`），需要整数 ID 请用 `XML_REMOVENODE`。
