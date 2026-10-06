# XML_REPLACE

- **类别**：式中函数（EM 扩展 / XML 文档表）
- **签名**：
  - `int XML_REPLACE(<id>, str 新xml)`（整文档替换）
  - `int XML_REPLACE(<id>, str xpath, str 新xml{, int 全部替换})`
  - `int XML_REPLACE(<xmlStrVar>, str xpath, str 新xml{, int 全部替换})`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:191-197` 有记载（「◆ <1> int XML_REPLACE xml, xml2 ／ <2> int XML_REPLACE xml, xpath, xml2(, setall)」，并注明「バリアント<1>の第1引数は整数だけ有効となる」），`Emuera.EM_changelog.txt:13`（v5）追加。语义以源码为准。

## 语义

按 XPath 选出节点并用一段新 XML 替换，或者整体替换一篇已保存的文档。两种用法：

- **形式 1（2 个参数）**：`XML_REPLACE(id, 新xml)` —— 把文档表中该 ID 的文档**整篇换成**新解析出来的文档（`dict[idx] = newXml`）。ID 不存在返回 `-1`，成功返回 `1`（此分支返回的是 `1` 而不是匹配数）。
- **形式 2（3~4 个参数）**：`XML_REPLACE(目标, xpath, 新xml{, setall})` —— 用 XPath 选出节点，逐个替换为新 XML 的根元素节点，返回匹配到的节点个数。

`目标`（第 1 参数）的形态：

- **整数**：文档表中的 ID（形式 1 只在此形态下做整篇替换）。
- **字符串变量**（`RefString`）：内容当作 XML 文本解析，替换成功后**把结果写回该变量**。
- 形式 2 下若把字符串**字面量**放在第 1 参数，源码会走「字符串 + 参数个数为 2」之外的路径，即照常当作 XML 文本解析——但参数表（形式 1 是 `Any, String`，形式 2 要求 `Int` 或 `RefString`）意味着字面量字符串只在形式 1 合法，而形式 1 的字符串会被当作**文档名**（见备注）。

替换规则：**根节点不能被替换**（`node.ParentNode == null` 时失败）；匹配到**多个**节点时，第 4 参数为 `0` 或省略则**一个都不替换**；匹配恰好 1 个节点时总是尝试替换。替换内容取新 XML 的 `DocumentElement`（因此必须是单根元素）。单节点替换失败（根节点）返回 `0`。

## 用法

### int XML_REPLACE(id, 新xml)（整文档替换）
```erb
XML_DOCUMENT(0, "<old><a>1</a></old>")
PRINTL XML_REPLACE(0, "<new><b>2</b></new>")     ; 1
PRINTL XML_TOSTR(0)                               ; <new><b>2</b></new>

PRINTL XML_REPLACE(9, "<new/>")                   ; -1（ID 不存在）
```

### int XML_REPLACE(id, xpath, 新xml{, 全部替换})
```erb
XML_DOCUMENT(0, "<root><a>1</a><c>3</c></root>")

; 单个匹配：直接替换
PRINTL XML_REPLACE(0, "//a", "<a>NEW</a>")        ; 1
PRINTL XML_TOSTR(0)                                ; <root><a>NEW</a><c>3</c></root>

; 根节点不能被替换
PRINTL XML_REPLACE(0, "/root", "<r/>")             ; 0

; 多匹配必须给第 4 参数非 0
XML_DOCUMENT(1, "<root><b>1</b><b>2</b></root>")
PRINTL XML_REPLACE(1, "//b", "<b>X</b>", 0)        ; 2（但啥也没换）
PRINTL XML_TOSTR(1)                                ; <root><b>1</b><b>2</b></root>
PRINTL XML_REPLACE(1, "//b", "<b>X</b>", 1)        ; 2
PRINTL XML_TOSTR(1)                                ; <root><b>X</b><b>X</b></root>
```

### int XML_REPLACE(xmlStrVar, xpath, 新xml{, 全部替换})
- 第 1 参数是**字符串变量**，替换后该变量被改写为结果文本。
```erb
#DIMS DOC = 4
DOC = "<r><a>1</a></r>"
XML_REPLACE DOC, "//a", "<a>9</a>"
PRINTL DOC        ; <r><a>9</a></r>
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:258`（`["XML_REPLACE"] = new XmlReplaceMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1146`（`XmlReplaceMethod`；`Replace` 辅助在 `:1167`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlReplaceMethod:
    构造:
        ReturnType = long
        argumentTypeArrayEx = [
            { ArgTypes = { Any, String } },                                      # 恰好 2 个参数（形式 1）
            { ArgTypes = { Int, String, String, Int }, OmitStart = 3 },          # 3~4 个参数（整数 ID）
            { ArgTypes = { RefString, String, String, Int }, OmitStart = 3 }     # 3~4 个参数（字符串变量）
        ]
        CanRestructure = false

    Replace(node, newNode):             # :1167
        若 node.ParentNode != null: node.ParentNode.ReplaceChild(newNode, node); 返回 true
        否则: 返回 false                # 根节点 → 失败

    GetIntValue(exm, arguments):
        newXml = new XmlDocument()
        # 新 XML 文本：形式 2/3 在 arguments[2]，形式 1 在 arguments[1]
        xml = (参数个数 > 2) ? arguments[2] 的字符串值 : arguments[1] 的字符串值
        尝试 newXml.LoadXml(xml)  失败 → 抛 CodeEE(XmlParseError, Name, xml, e.Message)

        saveToArg0 = true
        若 arguments[0] 是整数
           或 (byName 且 arguments[0] 是字符串)
           或 (arguments[0] 是字符串 且 参数个数 == 2):        # ★ 2 参数 + 字符串第1参数 → 当作文档名
            saveToArg0 = false
            idx = arguments[0] 是字符串 ? 其字符串值 : 整数值.ToString()
            若 !文档表含 idx: 返回 -1
            若 参数个数 == 2:                                  # 形式 1：整篇替换
                表[idx] = newXml
                返回 1                                          # 注意返回 1，不是匹配数
            doc = 表[idx]
        否则:
            doc = new XmlDocument(); doc.LoadXml(arguments[0] 的字符串值)
            失败 → 抛 CodeEE(XmlParseError, Name, xml, e.Message)     # 字符串变量形态

        path = arguments[1] 的字符串值
        尝试 nodes = doc.SelectNodes(path)
        失败(XPathException) → 抛 CodeEE(XmlXPathParseError, Name, path, e.Message)
        若 nodes.Count > 0:
            newNode = newXml.DocumentElement
            child = doc.CreateNode(newNode.NodeType, newNode.Name, newNode.NamespaceURI)
            复制 newNode 的属性到 child
            child.InnerXml = newNode.InnerXml
            setAllNodes = (参数个数 >= 4) ? arguments[3] != 0 : false
            若 nodes.Count != 1:
                若 setAllNodes: for i: Replace(nodes[i], child)      # 多匹配且 setall=0 → 不替换
            否则:
                若 !Replace(nodes[0], child): 返回 0                  # 单匹配且是根节点 → 0
            若 saveToArg0: (arguments[0] as VariableTerm).SetValue(doc.OuterXml, exm)
        返回 nodes.Count
```

## 备注

- 语义据源码；EM readme 的两个变体（`<1> xml, xml2`／`<2> xml, xpath, xml2(, setall)`）与「setall 为 0 或省略时不替换多个匹配结果」「成功返回匹配数、失败返回 0、不存在返回 -1」与源码一致。
- **readme 说「バリアント<1>の第1引数は整数だけ有効となる」（形式 1 只有整数第 1 参数有效），源码却多了一条 `arguments[0] 是字符串 且 参数个数 == 2` 的分支**：即 `XML_REPLACE("名字", "<新xml/>")` 会被当作「文档名为 `名字` 的整篇替换」处理（而不是把 `"名字"` 当 XML 解析）。这一点 readme 未记载，如实记录（读源码得，非推定；对纯按名替换的场景反而是便利写法，但要注意字符串第 1 参数不能被当作 XML 文本使用）。
- 形式 1 的返回值是 `1`（成功）／`-1`（ID 不存在），**不是**匹配数；形式 2 才是匹配数。同一函数两种返回含义，容易混用。
- 多匹配 + `全部替换 = 0` 时返回匹配数但一个都不换；根节点匹配时返回 `0`。
- 替换内容取新 XML 的**根元素**（单根要求），属性与内部内容被复制；命名空间/前缀等会被 `CreateNode` + `InnerXml` 赋值的 .NET 行为规范化（标注为推定）。
- 与 `XML_SET` 的区别：`XML_SET` 只改节点的值/文本，`XML_REPLACE` 换掉整个节点子树；`XML_REPLACE` 不能换根节点，`XML_SET` 可以改根节点的 `InnerText`/`InnerXml`。
