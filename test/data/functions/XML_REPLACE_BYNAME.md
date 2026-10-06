# XML_REPLACE_BYNAME

- **类别**：式中函数（EM 扩展 / XML 文档表，`XML_REPLACE` 的按名变体）
- **签名**：`int XML_REPLACE_BYNAME(str 文档名, str xpath, str 新xml{, int 全部替换})`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）、EE readme、本分支 EM readme 均未收录 `_BYNAME` 变体（EM readme 只写了 `XML_REPLACE`，`Emuera.EM_readme.txt:191`）。语义据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:1146` 的 `XmlReplaceMethod`，注册时传 `byname: true`），属无外部文档、纯按源码编写的条目。

## 语义

与 `XML_REPLACE` 同一实现，差别有两处：

1. 参数表只剩一种形态：`String, String, String, Int`（`OmitStart = 3`，即 **3~4 个参数**）——**第 1 参数只能是字符串**，且一律被当作**文档表的名字**。因此**没有形式 1（两参数整篇替换）**：`XML_REPLACE_BYNAME("名", "<新xml/>")` 只有 2 个参数，会在解析期因参数不足而报错。
2. 没有「回写变量」的行为：替换只发生在文档表里，用 `XML_TOSTR(文档名)` 取回结果。

其余语义与 `XML_REPLACE` 的形式 2 相同：用 XPath 选节点替换、根节点不能替换（返回 `0`）、多匹配需第 4 参数非 `0` 才替换、文档名不存在返回 `-1`、XPath/XML 错误抛 `CodeEE`。

## 用法

### int XML_REPLACE_BYNAME(文档名, xpath, 新xml{, 全部替换})
- 文档名：文档表里的名字（字符串）。
- xpath：定位待替换的节点。
- 新xml：替换用的单根元素 XML 文本。
- 全部替换：匹配多个节点时，`0`/省略 → 一个都不换；非 `0` → 逐个替换。
- 返回值：匹配个数；单节点替换失败（根节点）`0`；文档不存在 `-1`。
```erb
XML_DOCUMENT("save1", "<root><a>1</a></root>")

PRINTL XML_REPLACE_BYNAME("save1", "//a", "<a>NEW</a>")     ; 1
PRINTL XML_TOSTR("save1")                                    ; <root><a>NEW</a></root>

PRINTL XML_REPLACE_BYNAME("save1", "/root", "<r/>")          ; 0（根节点不能替换）
PRINTL XML_REPLACE_BYNAME("nope", "//a", "<a/>")             ; -1（文档不存在）
```

```erb
; 多匹配必须显式给第 4 参数非 0
XML_DOCUMENT("list", "<r><i>1</i><i>2</i></r>")
XML_REPLACE_BYNAME "list", "//i", "<i>X</i>", 0
PRINTL XML_TOSTR("list")        ; <r><i>1</i><i>2</i></r>（未替换）
XML_REPLACE_BYNAME "list", "//i", "<i>X</i>", 1
PRINTL XML_TOSTR("list")        ; <r><i>X</i><i>X</i></r>
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:259`（`["XML_REPLACE_BYNAME"] = new XmlReplaceMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1146`（`XmlReplaceMethod`；`byName` 在 `:1165`，构造覆写在 `:1158`，`Replace` 在 `:1167`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlReplaceMethod（byname = true 的构造）:
    构造(bool byname):
        ReturnType = long
        byName = byname
        argumentTypeArrayEx = [
            { ArgTypes = { String, String, String, Int }, OmitStart = 3 }   # ★ 只剩字符串形态，3~4 个参数
        ]                                                                   # → 没有「两参数整篇替换」形式
        CanRestructure = false

    Replace(node, newNode):             # :1167
        若 node.ParentNode != null: node.ParentNode.ReplaceChild(newNode, node); 返回 true
        否则: 返回 false                # 根节点 → 失败

    GetIntValue(exm, arguments):
        newXml = new XmlDocument()
        xml = (参数个数 > 2) ? arguments[2] 的字符串值 : arguments[1] 的字符串值   # 参数表保证 ≥3 → 恒取 arguments[2]
        尝试 newXml.LoadXml(xml) → 失败抛 CodeEE(XmlParseError, Name, xml, e.Message)
        saveToArg0 = true
        若 arguments[0] 是整数 或 (byName 且 arguments[0] 是字符串):     # ★ 字符串一律当文档名
            saveToArg0 = false
            idx = arguments[0] 的字符串值
            若 !文档表含 idx: 返回 -1
            若 参数个数 == 2: 表[idx] = newXml; 返回 1          # 参数表不允许 2 个参数 → 不可达
            doc = 表[idx]
        否则: （本变体不可达）
        path = arguments[1]；SelectNodes 失败 → CodeEE(XmlXPathParseError, ...)
        若 nodes.Count > 0:
            child = doc.CreateNode(...)；复制属性；child.InnerXml = newNode.InnerXml
            setAllNodes = (参数个数 >= 4) ? arguments[3] != 0 : false
            若 nodes.Count != 1: 若 setAllNodes: 逐个 Replace
            否则: 若 !Replace(nodes[0], child): 返回 0
            若 saveToArg0: 回写（本变体恒 false，不执行）
        返回 nodes.Count
```

## 备注

- **无外部文档**：`XML_REPLACE_BYNAME` 未出现在两套中文文档、EE readme、EM readme 中；本文据源码编写。与 `XML_REPLACE` 的差异即「字符串第 1 参数 = 文档名」与「参数表不再允许 2 个参数（无整篇替换形式）」两点。
- **无法用本函数做「整篇替换」**：参数表 `OmitStart = 3` 要求至少 3 个参数，而整篇替换的代码分支要求恰好 2 个参数，二者矛盾 → 该分支对本变体不可达。要整篇换文档，请用整数 ID 的 `XML_REPLACE(id, 新xml)`，或者 `XML_RELEASE` + `XML_DOCUMENT`。
- 文档名区分大小写（`Dictionary` 默认 `Ordinal` 比较）。
- 根节点不能替换（返回 `0`）；多匹配 + `全部替换 = 0` 时返回匹配数但一个都不换。
- 替换内容必须是单根元素 XML（取 `DocumentElement`）；属性与内容会被复制，格式由 .NET 规范化（标注为推定）。
