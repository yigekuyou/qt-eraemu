# XML_REMOVEATTRIBUTE

- **类别**：式中函数（EM 扩展 / XML 文档表）
- **签名**：
  - `int XML_REMOVEATTRIBUTE(<id>, str xpath{, int 全部删除})`
  - `int XML_REMOVEATTRIBUTE(<xmlStrVar>, str xpath{, int 全部删除})`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:208-212` 有记载（「◆ int XML_REMOVEATTRIBUTE xml, xpath(, setall)」），`Emuera.EM_changelog.txt:13`（v5）追加。语义以源码为准。

## 语义

用 XPath 选出**属性节点**并删除，返回匹配到的属性个数。与 `XML_REMOVENODE` 是同一实现类的两个 `Operation`（本函数为 `Attribute`）。第 1 参数：

- **整数**：文档表中的 ID，直接改写已保存的文档。
- **字符串变量**（`RefString`）：把变量内容当作 XML 文本解析，删除成功后**把结果写回该变量**。

删除规则：

- XPath 必须选出**属性节点**（如 `//a/@id`、`//@*`）。若选出的是元素节点，删除会失败：单节点时返回 `0`（且不回写变量），多节点时被逐个跳过（返回值仍是匹配数）。
- 匹配到**多个**属性时，第 3 参数（全部删除）为 `0` 或省略则**一个都不删**；非 `0` 才逐个删除。
- 匹配到**恰好 1 个**属性时总是尝试删除。

文档 ID 不存在 → `-1`；XPath 错误 → `CodeEE`；XML 文本非法 → `CodeEE`。

## 用法

### int XML_REMOVEATTRIBUTE(id, xpath{, 全部删除})
```erb
XML_DOCUMENT(0, "<root><a id=""1"" x=""9"">t</a></root>")

; 单个属性：直接删
PRINTL XML_REMOVEATTRIBUTE(0, "//a/@x")        ; 1
PRINTL XML_TOSTR(0)                             ; <root><a id="1">t</a></root>

; 选中的是元素而非属性 → 删除失败
PRINTL XML_REMOVEATTRIBUTE(0, "//a")           ; 0

; 通配所有属性，需 setall 非 0
PRINTL XML_REMOVEATTRIBUTE(0, "//a/@*")        ; 1（只剩 id 一个属性，单匹配 → 总是删）
PRINTL XML_TOSTR(0)                             ; <root><a>t</a></root>
```

### int XML_REMOVEATTRIBUTE(xmlStrVar, xpath{, 全部删除})
- 第 1 参数是**字符串变量**，删除后该变量被改写为结果文本。
```erb
#DIMS DOC = 4
DOC = "<r><i a=""1"" b=""2"">x</i></r>"

XML_REMOVEATTRIBUTE DOC, "//i/@*", 0
PRINTL DOC        ; <r><i a="1" b="2">x</i></r>（多匹配 + setall=0，未删）
XML_REMOVEATTRIBUTE DOC, "//i/@*", 1
PRINTL DOC        ; <r><i>x</i></r>
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:262`（`["XML_REMOVEATTRIBUTE"] = new XmlRemoveNodeMethod(XmlRemoveNodeMethod.Operation.Attribute)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1048`（`XmlRemoveNodeMethod`；`Remove` 的 `Attribute` 分支在 `:1072-1079`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlRemoveNodeMethod（op = Attribute）:
    构造:
        ReturnType = long
        argumentTypeArrayEx = [
            { ArgTypes = { Int, String, Int }, OmitStart = 2 },         # 2~3 个参数（整数 ID）
            { ArgTypes = { RefString, String, Int }, OmitStart = 2 }    # 2~3 个参数（字符串变量）
        ]
        CanRestructure = false

    Remove(node):                       # :1070
        若 op == Attribute:
            若 node 是 XmlAttribute:
                attr.OwnerElement.Attributes.Remove(attr)           # 从所属元素的属性表移除
                返回 true
            # 不是属性节点 → 落到末尾
        否则: （Node 分支，见 XML_REMOVENODE）
        返回 false                       # 属性分支失败时返回 false

    GetIntValue(exm, arguments):
        method = (参数个数 >= 4) ? arguments[3] : 0     # 死代码（参数表最多 3 个），且未被使用
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
                若 setAllNodes: for i: Remove(nodes[i])       # 多匹配 + setall=0 → 不删
            否则:
                若 !Remove(nodes[0]): 返回 0                   # 单匹配删除失败 → 0（不回写）
            若 saveToArg0: (arguments[0] as VariableTerm).SetValue(doc.OuterXml, exm)
        返回 nodes.Count
```

## 备注

- 语义据源码；EM readme 的「删除 xpath 选中的属性」「setall 为 0 或省略时不删除多个匹配结果」「成功返回匹配数、失败返回 0、文档不存在返回 -1」与源码一致。
- **必须用能选出属性的 XPath**：属性节点要用 `@` 轴（`//a/@id`、`//@*`）。选中元素时 `Remove` 的 `InstanceOfType` 检查失败 → 单匹配返回 `0`、多匹配静默跳过（返回值仍为匹配数）。
- **死代码**：同 `XML_REMOVENODE`，`method` 变量（`:1094`）恒为 0 且未被使用，是从 `XML_ADDNODE` 复制来的残留。
- 第 1 参数为字符串变量时回写整篇 `doc.OuterXml`（格式会被规范化）；整数 ID 形式不回写。
- 多匹配 + `全部删除 = 0` 时返回匹配数但一个都不删——批量删属性务必显式给第 3 参数非 `0`。
- 与 `XML_REMOVENODE` 共享实现类，因此两者的「返回值语义」「setall 门槛」「根节点/类型不匹配的处理」完全一致；`XML_REMOVENODE_BYNAME` 与此函数的 `_BYNAME` 变体同理（见各文档）。
