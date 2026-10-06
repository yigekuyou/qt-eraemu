# XML_REMOVENODE

- **类别**：式中函数（EM 扩展 / XML 文档表）
- **签名**：
  - `int XML_REMOVENODE(<id>, str xpath{, int 全部删除})`
  - `int XML_REMOVENODE(<xmlStrVar>, str xpath{, int 全部删除})`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:185-189` 有记载（「◆ int XML_REMOVENODE xml, xpath(, setall)」），`Emuera.EM_changelog.txt:13`（v5）追加。语义以源码为准。

## 语义

用 XPath 选出**元素节点**并从文档中删除，返回匹配到的节点个数。第 1 参数：

- **整数**：文档表中的 ID，直接改写已保存的文档（用 `XML_TOSTR` 可取回结果）。
- **字符串变量**（`RefString`）：把变量内容当作 XML 文本解析，删除成功后**把结果写回该变量**。

删除规则（源码 `Remove`）：

- **根节点删不掉**：只有存在父节点的节点才会被移除；若恰好只匹配到根节点，返回 `0`。
- 匹配到**多个**节点时，第 3 参数（全部删除）为 `0` 或省略则**一个都不删**；非 `0` 才逐个删除。
- 匹配到**恰好 1 个**节点时总是尝试删除，不看第 3 参数。

文档 ID 不存在 → 返回 `-1`；XPath 语法错误 → `CodeEE`；第 1 参数为字符串变量且 XML 非法 → `CodeEE`。

## 用法

### int XML_REMOVENODE(id, xpath{, 全部删除})
```erb
XML_DOCUMENT(0, "<root><a>1</a><b>2</b><c>3</c></root>")

; 单个匹配：直接删
PRINTL XML_REMOVENODE(0, "//b")            ; 1
PRINTL XML_TOSTR(0)                         ; <root><a>1</a><c>3</c></root>

; 根节点删不掉
PRINTL XML_REMOVENODE(0, "/root")          ; 0（无父节点，删除失败）
```

### int XML_REMOVENODE(xmlStrVar, xpath{, 全部删除})
- 第 1 参数是**字符串变量**，删除后该变量被改写为结果文本。
```erb
#DIMS DOC = 4
DOC = "<root><a>1</a><a>2</a></root>"

XML_REMOVENODE DOC, "//a", 0
PRINTL DOC                                  ; <root><a>1</a><a>2</a></root>（多匹配 + setall=0，未删）
XML_REMOVENODE DOC, "//a", 1
PRINTL DOC                                  ; <root />
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:256`（`["XML_REMOVENODE"] = new XmlRemoveNodeMethod(XmlRemoveNodeMethod.Operation.Node)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1048`（`XmlRemoveNodeMethod`；`Remove` 辅助在 `:1070`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlRemoveNodeMethod（op = Node）:
    构造:
        ReturnType = long
        argumentTypeArrayEx = [
            { ArgTypes = { Int, String, Int }, OmitStart = 2 },         # 2~3 个参数（整数 ID）
            { ArgTypes = { RefString, String, Int }, OmitStart = 2 }    # 2~3 个参数（字符串变量）
        ]
        CanRestructure = false

    Remove(node):                       # :1070
        若 op == Attribute:
            若 node 是 XmlAttribute: attr.OwnerElement.Attributes.Remove(attr); 返回 true
        否则（op == Node）:
            若 node.ParentNode != null: node.ParentNode.RemoveChild(node); 返回 true
            否则: 返回 false            # 根节点 → 失败
        返回 false

    GetIntValue(exm, arguments):
        method = (参数个数 >= 4) ? arguments[3] : 0     # ★ 参数表最多 3 个，本行恒取 0（死代码，且 method 未被使用）
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
            setAllNodes = (参数个数 == 3) ? arguments[2] != 0 : false
            若 nodes.Count != 1:
                若 setAllNodes: for i: Remove(nodes[i])       # 多节点且 setall=0 → 不删
            否则:
                若 !Remove(nodes[0]): 返回 0                   # 单节点删除失败（根节点）→ 0，且不回写
            若 saveToArg0: (arguments[0] as VariableTerm).SetValue(doc.OuterXml, exm)
        返回 nodes.Count
```

## 备注

- 语义据源码；EM readme 的「删除 xpath 选中的元素节点（根节点无效）」「setall 为 0 或省略时对多个匹配结果不删除」「成功返回匹配数、失败返回 0、文档不存在返回 -1」与源码一致。
- **死代码**：`method` 的计算（`Runtime/Script/Statements/Function/Creator.Method.cs:1094`）来自 `XML_ADDNODE` 的复制粘贴，本函数参数最多 3 个，该表达式恒为 `0`，且计算结果再也没有被使用。如实记录（读源码得，非推定）。
- 第 1 参数为字符串变量时**回写整篇文档文本**（`doc.OuterXml`），格式会被规范化；文档 ID 形式则不回写、结果留在文档表内。
- 多节点 + `全部删除 = 0` 时返回匹配数但一个都不删——这是最容易踩的坑；删多个元素必须显式给第 3 参数非 `0`。
- 与 `XML_REMOVEATTRIBUTE` 是同一实现类（`Operation` 不同）：本函数删元素，后者删属性。用本函数去删属性（XPath `//@attr`）不会成功（`Remove` 只处理 `ParentNode`，属性节点的 `ParentNode` 为 null）。
