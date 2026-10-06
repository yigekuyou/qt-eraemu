# XML_ADDNODE_BYNAME

- **类别**：式中函数（EM 扩展 / XML 文档表，`XML_ADDNODE` 的按名变体）
- **签名**：`int XML_ADDNODE_BYNAME(str 文档名, str xpath, str 子节点xml{, int 添加方式, int 全部添加})`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）、EE readme、本分支 EM readme 均未收录 `_BYNAME` 变体（EM readme 只写了 `XML_ADDNODE`，`Emuera.EM_readme.txt:176`）。语义据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:893` 的 `XmlAddNodeMethod`，注册时传 `byname: true`），属无外部文档、纯按源码编写的条目。

## 语义

与 `XML_ADDNODE` 同一实现，差别有两处：

1. 参数表只剩一种形态：`String, String, String, Int, Int`（`OmitStart = 3`，即 3~5 个参数）——**第 1 参数只能是字符串**，且一律被当作**文档表的名字**。
2. 没有「回写变量」的行为：插入只发生在文档表里，用 `XML_TOSTR(文档名)` 取回结果。

其余语义与 `XML_ADDNODE` 相同：第 3 参数必须是单根元素的 XML；添加方式 `0`=末尾子节点、`1`=前一兄弟、`2`=后一兄弟（越界夹回 `0`）；多匹配需第 5 参数非 `0` 才插入（且该参数只有在参数个数正好为 5 时才被读取）；单匹配且方式 1/2 失败返回 `0`；文档名不存在返回 `-1`。

## 用法

### int XML_ADDNODE_BYNAME(文档名, xpath, 子节点xml{, 添加方式, 全部添加})
- 文档名：文档表里的名字（字符串）。
- xpath：定位父节点（或兄弟参照节点）。
- 子节点xml：要插入的单根元素 XML 文本。
- 添加方式：`0` 追加为子节点（默认）；`1` 插在前；`2` 插在后。
- 全部添加：匹配多个节点时，`0`/省略 → 一个都不插；非 `0` → 逐个插入。
- 返回值：匹配个数；单节点插入失败 `0`；文档不存在 `-1`。
```erb
XML_DOCUMENT("save1", "<root><a/></root>")

XML_ADDNODE_BYNAME "save1", "//a", "<b>1</b>"
PRINTL XML_TOSTR("save1")                                  ; <root><a><b>1</b></a></root>

; 作为 //a 的后一个兄弟
XML_ADDNODE_BYNAME "save1", "//a", "<c/>", 2, 0
PRINTL XML_TOSTR("save1")                                  ; <root><a><b>1</b></a><c /></root>

PRINTL XML_ADDNODE_BYNAME("nope", "//a", "<b/>")            ; -1
```

```erb
; 多匹配必须显式给「全部添加」非 0
XML_DOCUMENT("list", "<r><i/><i/></r>")
XML_ADDNODE_BYNAME "list", "//i", "<v/>", 0, 1
PRINTL XML_TOSTR("list")        ; <r><i><v /></i><i><v /></i></r>
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:255`（`["XML_ADDNODE_BYNAME"] = new XmlAddNodeMethod(XmlAddNodeMethod.Operation.Node, true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:893`（`XmlAddNodeMethod`；`byName` 在 `:924`，构造覆写在 `:912`，`Insert` 在 `:926`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlAddNodeMethod（op = Node, byname = true 的构造）:
    构造(op, byname):
        ReturnType = long
        byName = byname
        argumentTypeArrayEx = [
            { ArgTypes = { String, String, String, Int, Int }, OmitStart = 3 }   # ★ 只剩字符串形态，3~5 个参数
        ]
        CanRestructure = false

    Insert(node, child, method): 见 XML_ADDNODE（method 1/2 需要 node.ParentNode != null）

    GetIntValue(exm, arguments):
        methodPos = 4
        method = (参数个数 >= 4) ? arguments[3] 的整数值 : 0；越界夹回 0
        saveToArg0 = true
        若 arguments[0] 是整数 或 (byName 且 arguments[0] 是字符串):     # ★ 字符串一律当文档名
            saveToArg0 = false
            idx = arguments[0] 的字符串值；文档表含 idx ? doc = 表[idx] : 返回 -1
        否则: （本变体不可达）解析 XML 文本；失败 → CodeEE(XmlParseError, ...)
        path = arguments[1]；SelectNodes 失败 → CodeEE(XmlXPathParseError, ...)
        若 nodes.Count > 0:
            setAllPos = 5
            setAllNodes = (参数个数 == 5) ? arguments[4] != 0 : false
            childNode = new XmlDocument(); childNode.LoadXml(arguments[2])   # 单根元素
            newNode = childNode.DocumentElement
            child = doc.CreateNode(...)；复制属性；child.InnerXml = newNode.InnerXml
            若 nodes.Count != 1: 若 setAllNodes: 逐个 Insert(nodes[i], child, method)
            否则: 若 !Insert(nodes[0], child, method) 且 method > 0: 返回 0
            若 saveToArg0: 回写（本变体恒 false，不执行）
        返回 nodes.Count
```

## 备注

- **无外部文档**：`XML_ADDNODE_BYNAME` 未出现在两套中文文档、EE readme、EM readme 中；本文据源码编写。与 `XML_ADDNODE` 的差异即「字符串第 1 参数 = 文档名」与「没有 `RefString` 形态」两点。
- 文档名区分大小写（`Dictionary` 默认 `Ordinal` 比较）。
- 与 `XML_ADDNODE` 共享实现，因此注意项相同：第 3 参数必须单根；`setall` 只在参数个数正好 5 时读取；方式 `1`/`2` 用在根节点上会失败（单匹配返回 `0`）；多匹配共用同一个 `child` 实例，实际只需最后一次插入生效（据 .NET DOM 语义推得，标注为推定）。
- 字符串形态不支持整数 ID，需要整数 ID 请用 `XML_ADDNODE`。
