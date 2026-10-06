# XML_ADDNODE

- **类别**：式中函数（EM 扩展 / XML 文档表）
- **签名**：
  - `int XML_ADDNODE(<id>, str xpath, str 子节点xml{, int 添加方式, int 全部添加})`
  - `int XML_ADDNODE(<xmlStrVar>, str xpath, str 子节点xml{, int 添加方式, int 全部添加})`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:176-183` 有记载（「◆ int XML_ADDNODE xml, xpath, xml(, addmethod, setall)」，含三种 addmethod 的定义），`Emuera.EM_changelog.txt:13`（v5）追加。语义以源码为准。

## 语义

用 XPath 选出一个**元素节点**，把一段新的 XML 作为节点插入，返回匹配到的节点个数。第 1 参数：

- **整数**：文档表中的 ID，直接改写已保存的文档（用 `XML_TOSTR` 取回）。
- **字符串变量**（`RefString`）：把变量内容当作 XML 文本解析，插入成功后**把结果写回该变量**。

第 3 参数是作为子（或兄弟）节点加入的 XML 文本，必须是**单根元素**（源码取 `DocumentElement`），其属性与内部内容会被复制到新节点上。

第 4 参数（添加方式，源码按 `int method` 夹到 `0..2`）：

- `0` 或省略：作为 xpath 选中节点的**最后一个子节点**追加（`AppendChild`）。
- `1`：作为选中节点的**前一个兄弟节点**插入（`ParentNode.InsertBefore`）；选中节点是根节点（无父）时插入失败。
- `2`：作为选中节点的**后一个兄弟节点**插入（`ParentNode.InsertAfter`）；同样是根节点时失败。

第 5 参数（全部添加）：匹配到**多个**节点时，为 `0` 或省略则**一个都不插**；非 `0` 才逐个插入。匹配恰好 1 个节点时总是插入。

返回值：匹配到的节点个数；单节点插入失败（如方式 1/2 用在根节点上）返回 `0`；文档 ID 不存在返回 `-1`；XPath 或 XML 错误抛 `CodeEE`。无匹配时返回 `0`。

## 用法

### int XML_ADDNODE(id, xpath, 子节点xml{, 添加方式, 全部添加})
```erb
XML_DOCUMENT(0, "<root><a>1</a></root>")

; 追加到 //a 内部
PRINTL XML_ADDNODE(0, "//a", "<b>new</b>")          ; 1
PRINTL XML_TOSTR(0)                                  ; <root><a>1<b>new</b></a></root>

; 作为 //a 的后一个兄弟
PRINTL XML_ADDNODE(0, "//a", "<c/>", 2)             ; 1
PRINTL XML_TOSTR(0)                                  ; <root><a>1<b>new</b></a><c /></root>

; 根节点没有兄弟 → 失败
PRINTL XML_ADDNODE(0, "/root", "<z/>", 1)           ; 0
```

### int XML_ADDNODE(xmlStrVar, xpath, 子节点xml{, 添加方式, 全部添加})
- 第 1 参数是**字符串变量**，插入后该变量被改写为结果文本。
```erb
#DIMS DOC = 4
DOC = "<r><x/></r>"

; 多匹配 + 全部添加=0 → 不插
XML_ADDNODE DOC, "//x", "<y/>", 0, 0
PRINTL DOC        ; <r><x /></r>
XML_ADDNODE DOC, "//x", "<y/>", 0, 1
PRINTL DOC        ; <r><x><y /></x></r>
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:254`（`["XML_ADDNODE"] = new XmlAddNodeMethod(XmlAddNodeMethod.Operation.Node)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:893`（`XmlAddNodeMethod`；`Insert` 辅助在 `:926`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlAddNodeMethod（op = Node）:
    构造:
        ReturnType = long
        argumentTypeArrayEx = [
            { ArgTypes = { Int, String, String, Int, Int }, OmitStart = 3 },        # 3~5 个参数（整数 ID）
            { ArgTypes = { RefString, String, String, Int, Int }, OmitStart = 3 }   # 3~5 个参数（字符串变量）
        ]
        CanRestructure = false

    Insert(node, child, method):        # :926
        若 op == Node:
            method 0 → node.AppendChild(child)
            method 1 → node.ParentNode == null ? 返回 false : node.ParentNode.InsertBefore(child, node)
            method 2 → node.ParentNode == null ? 返回 false : node.ParentNode.InsertAfter(child, node)
            返回 true
        否则（Attribute 分支，见 XML_ADDATTRIBUTE）

    GetIntValue(exm, arguments):
        methodPos = 4                                   # Node 时：添加方式在 arguments[3]
        method = (参数个数 >= methodPos) ? (int)arguments[methodPos - 1] 的整数值 : 0
        若 method > 2 或 < 0: method = 0
        saveToArg0 = true
        若 arguments[0] 是整数 或 (byName 且 arguments[0] 是字符串):
            saveToArg0 = false
            idx = ...；文档表含 idx ? doc = 表[idx] : 返回 -1
        否则:
            doc = new XmlDocument(); doc.LoadXml(arguments[0] 的字符串值)
            失败 → 抛 CodeEE(XmlParseError, Name, xml, e.Message)
        path = arguments[1]；SelectNodes 失败 → 抛 CodeEE(XmlXPathParseError, ...)
        若 nodes.Count > 0:
            setAllPos = 5                               # Node 时：全部添加在 arguments[4]，且必须正好 5 个参数
            setAllNodes = (参数个数 == setAllPos) ? arguments[setAllPos-1] != 0 : false
            # 构造要插入的节点
            childNode = new XmlDocument()
            尝试 childNode.LoadXml(arguments[2] 的字符串值)     # 必须是单根元素
            失败 → 抛 CodeEE(XmlParseError, Name, xml, e.Message)
            newNode = childNode.DocumentElement
            child = doc.CreateNode(newNode.NodeType, newNode.Name, newNode.NamespaceURI)
            逐个复制 newNode 的属性到 child
            child.InnerXml = newNode.InnerXml                   # 内部 XML 原样拷入
            若 nodes.Count != 1:
                若 setAllNodes: for i: Insert(nodes[i], child, method)     # 多匹配且 setall=0 → 不插
            否则:
                若 !Insert(nodes[0], child, method) 且 method > 0: 返回 0   # 单匹配且方式 1/2 失败 → 0
            若 saveToArg0: (arguments[0] as VariableTerm).SetValue(doc.OuterXml, exm)
        返回 nodes.Count
```

## 备注

- 语义据源码；EM readme 的三种 addmethod 定义（0=末尾子节点、1=前一兄弟、2=后一兄弟）与「setall 为 0 或省略时对多个匹配结果不添加」「成功返回匹配数、失败返回 0、文档不存在返回 -1」与源码一致。
- **多匹配 + 全部添加 = 0 时什么都不做**（返回匹配数）；这是最容易踩的坑。同时注意 `setall` 只有在参数个数**正好**为 5 时才被读取（`arguments.Count == setAllPos`），所以「给 setall 就必须要先给 addmethod」。
- 第 3 参数的 XML 必须是**单根元素**：源码用 `DocumentElement` 取根；传入多根或纯文本（非良构 XML）会抛 `CodeEE`。
- 新节点的属性与内部内容会被复制，但**命名空间前缀/声明与文本节点会被规范化**（`CreateNode` + `InnerXml` 赋值）；这一点属 .NET 行为，标注为推定。
- `method = 1/2` 时若选中节点是根节点（无 `ParentNode`）会插入失败：单匹配 → 返回 `0` 且不回写；多匹配 → 该节点被跳过（不报错）。
- 第 1 参数为字符串变量时回写整篇 `doc.OuterXml`（格式会被规范化）；整数 ID 形式不回写。
- 兄弟关系是在 `doc` 里建立的：`child` 由 `doc.CreateNode` 创建，因此插入到别的节点下合法；但注意**多个匹配共用同一个 `child` 实例**（源码只在循环外创建一次），逐个 `Insert` 时后一次会把同一节点从上一处移走（.NET 的 `AppendChild` 对已有父节点的节点执行移动而非复制）。因此「多匹配 + 全部添加」实际上只会保留最后一次插入的位置——此结论据 .NET DOM 语义推得，标注为推定。
