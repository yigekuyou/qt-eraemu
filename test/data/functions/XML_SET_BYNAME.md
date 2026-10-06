# XML_SET_BYNAME

- **类别**：式中函数（EM 扩展 / XML 文档表，`XML_SET` 的按名变体）
- **签名**：`int XML_SET_BYNAME(str 文档名, str xpath, str 值{, int 全部设置, int 设置风格})`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）、EE readme、本分支 EM readme 均未收录 `_BYNAME` 变体（EM readme 只写了 `XML_SET`，`Emuera.EM_readme.txt:162`）。语义据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:791` 的 `XmlSetMethod`，注册时传 `byname: true`），属无外部文档、纯按源码编写的条目。

## 语义

与 `XML_SET` 同一实现，差别有两处：

1. 参数表只剩一种形态：`String, String, String, Int, Int`（`OmitStart = 3`，即 3~5 个参数）——**第 1 参数只能是字符串**，且一律被当作**文档表的名字**（不会被当 XML 文本解析，也没有「整数 ID」这一写法；但源码的整数判断仍在，只是参数表不允许整数）。
2. 第 1 参数不是变量、也不回写：改写直接留在文档表中（`saveToArg0 = false` 的那条分支）。

其余语义与 `XML_SET` 相同：返回匹配的节点个数，文档不存在返回 `-1`，XPath 错误抛 `CodeEE`，多节点需第 4 参数非 `0` 才写入，第 5 参数 `1`=InnerText、`2`=InnerXml、其他=Value（越界夹回 0）。

## 用法

### int XML_SET_BYNAME(文档名, xpath, 值{, 全部设置, 设置风格})
- 文档名：文档表里的名字（字符串）。
- xpath：XPath 表达式。
- 值：要写入的字符串。
- 全部设置：匹配多个节点时，`0`/省略 → 一个都不改；非 `0` → 全部写入。
- 设置风格：`1` InnerText / `2` InnerXml / 其他 Value。
- 返回值：匹配个数；文档不存在 `-1`。
```erb
XML_DOCUMENT("save1", "<root><name>old</name></root>")

PRINTL XML_SET_BYNAME("save1", "//name", "Alice", 0, 1)   ; 1
PRINTL XML_TOSTR("save1")                                  ; <root><name>Alice</name></root>

PRINTL XML_SET_BYNAME("nope", "//name", "X")               ; -1（文档不存在）
```

```erb
; 多节点批量改
XML_DOCUMENT("list", "<r><i>1</i><i>2</i><i>3</i></r>")
XML_SET_BYNAME "list", "//i", "0", 1, 1
PRINTL XML_TOSTR("list")        ; <r><i>0</i><i>0</i><i>0</i></r>
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:251`（`["XML_SET_BYNAME"] = new XmlSetMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:791`（`XmlSetMethod`；`byName` 字段在 `:809`，构造覆写在 `:802`，`SetNode` 在 `:810`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlSetMethod（byname = true 的构造）:
    构造(bool byname):
        ReturnType = long
        byName = byname
        argumentTypeArrayEx = [
            { ArgTypes = { String, String, String, Int, Int }, OmitStart = 3 }   # ★ 只剩字符串形态，3~5 个参数
        ]
        CanRestructure = false
    GetIntValue(exm, arguments):
        saveToArg0 = true
        若 arguments[0] 是整数 或 (byName 且 arguments[0] 是字符串):     # ★ byName 让字符串一律当文档名
            saveToArg0 = false
            idx = arguments[0] 的字符串值（或整数值.ToString()，参数表已禁止整数）
            若 文档表含 idx: doc = 表[idx]
            否则: 返回 -1
        否则: （本变体不可达）
            doc = 解析 arguments[0] 的字符串值；失败 → CodeEE(XmlParseError, ...)
        path = arguments[1]；解析失败 → CodeEE(XmlXPathParseError, ...)
        setAllNodes = (参数个数 >= 4) ? arguments[3] != 0 : false
        style       = (参数个数 == 5) ? arguments[4] : 0；越界夹回 0
        val = arguments[2]
        若 nodes.Count > 0:
            若 nodes.Count != 1: 若 setAllNodes: 逐个 SetNode
            否则: SetNode(nodes[0], val, style)
            若 saveToArg0: 回写 arguments[0]（本变体恒 false，不会执行）
        返回 nodes.Count
```

## 备注

- **无外部文档**：`XML_SET_BYNAME` 未出现在两套中文文档、EE readme、EM readme 中；本文据源码编写。与 `XML_SET` 的差异即「字符串第 1 参数 = 文档名」以及「没有 `RefString` 形态」两点。
- 与 `XML_SET` 的对照：`XML_SET("doc", ...)` 会把 `"doc"` 当成 XML 文本去解析（通常抛解析错误），`XML_SET_BYNAME("doc", ...)` 才会按文档名查找——这是 `_BYNAME` 系列存在的意义。
- 文档名区分大小写（`Dictionary` 默认 `Ordinal` 比较）。
- 风格 0（默认）= 设置 `XmlNode.Value`，对元素节点没有可写内容；写元素文本请用 `1`/`2`（据 .NET `XmlNode.Value` 语义推得，标注为推定）。
- 多节点 + `全部设置 = 0` 时返回匹配数但一个都不改；无匹配时返回 `0`。
