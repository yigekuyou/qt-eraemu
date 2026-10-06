# XML_SET

- **类别**：式中函数（EM 扩展 / XML 文档表）
- **签名**：
  - `int XML_SET(<id>, str xpath, str 值{, int 全部设置, int 设置风格})`
  - `int XML_SET(<xmlStrVar>, str xpath, str 值{, int 全部设置, int 设置风格})`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:162-170` 有记载（「◆ int XML_SET xml, xpath, str(, setall, int)」），`Emuera.EM_changelog.txt:11`（v5「XML_SET, XML_GETの説明文の修正」）、`:29`（v2 追加）。语义以源码为准。

## 语义

用 XPath 选出节点，把给定字符串写入这些节点，返回**匹配到的节点个数**。第 1 参数有两种形态：

- **整数**：文档表中的 ID，直接改写已保存的文档（此时第 1 参数不是变量，改写结果留在表里，用 `XML_TOSTR` 可取出）。
- **字符串变量**（`RefString`，必须是变量）：把变量内容当作 XML 文本解析，改写成功后**把结果写回该变量**（`doc.OuterXml`）。此时对文档表的改写不会发生（因为整篇文档只存在于变量里）。

第 2 参数为 XPath；语法错误 → `CodeEE`。文档 ID 不存在 → 返回 `-1`。

写入范围与风格：

- 第 4 参数（全部设置）：XPath 匹配到**多于 1 个节点**时，该值为 `0` 或省略则**一个都不改**；非 `0` 才逐个写入。匹配恰好 1 个节点时总是写入（不理会本参数）。
- 第 5 参数（设置风格）：`1` = 设置 `InnerText`、`2` = 设置 `InnerXml`、其他（含省略时的 `0`）= 设置 `Value`；源码把不在 `0..2` 的值夹回 `0`。
- 返回值：匹配到的节点个数（即使因「全部设置 = 0」而未写入，返回值仍是匹配数）。没有任何匹配时返回 `0`（也不会写回变量）。

## 用法

### int XML_SET(id, xpath, 值{, 全部设置, 设置风格})
```erb
XML_DOCUMENT(0, "<root><a>old</a></root>")
PRINTL XML_SET(0, "//a", "new", 0, 1)     ; 1
PRINTL XML_TOSTR(0)                        ; <root><a>new</a></root>
```

### int XML_SET(xmlStrVar, xpath, 值{, 全部设置, 设置风格})
- 第 1 参数必须是**字符串变量**，指向 XML 文本；调用后该变量被改写为结果文本。
```erb
#DIMS DOC = 4
DOC = "<root><a>1</a><a>2</a></root>"

; 多个匹配：必须给「全部设置 = 1」才会写
XML_SET DOC, "//a", "X", 0, 1
PRINTL DOC                                 ; <root><a>1</a><a>2</a></root>（未变）
XML_SET DOC, "//a", "X", 1, 1
PRINTL DOC                                 ; <root><a>X</a><a>X</a></root>

; 单个匹配：不需要「全部设置」
XML_SET DOC, "//a[1]", "Y", 0, 1
PRINTL DOC                                 ; <root><a>Y</a><a>X</a></root>
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:250`（`["XML_SET"] = new XmlSetMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:791`（`XmlSetMethod`；`SetNode` 辅助在 `:810`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlSetMethod:
    构造:
        ReturnType = long
        argumentTypeArrayEx = [
            { ArgTypes = { Int, String, String, Int, Int }, OmitStart = 3 },        # 3~5 个参数（整数 ID）
            { ArgTypes = { RefString, String, String, Int, Int }, OmitStart = 3 }   # 3~5 个参数（字符串变量）
        ]
        CanRestructure = false

    SetNode(node, val, style):         # :810
        style 1 → node.InnerText = val
        style 2 → node.InnerXml  = val
        其他    → node.Value     = val

    GetIntValue(exm, arguments):
        saveToArg0 = true
        若 arguments[0] 是整数 或 (byName 且 arguments[0] 是字符串):
            saveToArg0 = false                       # 改写留在文档表里
            idx = arguments[0] 是字符串 ? 其字符串值 : 整数值.ToString()
            若 文档表含 idx: doc = 表[idx]
            否则: 返回 -1                             # 文档不存在
        否则:
            doc = new XmlDocument()
            尝试 doc.LoadXml(arguments[0] 的字符串值)   # 第 1 参数是字符串变量
            失败(XmlException) → 抛 CodeEE(XmlParseError, Name, xml, e.Message)

        path = arguments[1] 的字符串值
        尝试 nodes = doc.SelectNodes(path)
        失败(XPathException) → 抛 CodeEE(XmlXPathParseError, Name, path, e.Message)

        setAllNodes = (参数个数 >= 4) ? (arguments[3] 的值 != 0) : false
        style       = (参数个数 == 5) ? arguments[4] 的整数值 : 0
        若 style > 2 或 style < 0: style = 0
        val = arguments[2] 的字符串值
        若 nodes.Count > 0:
            若 nodes.Count != 1:
                若 setAllNodes: for i: SetNode(nodes[i], val, style)    # 多节点且 setall=0 → 不写
            否则: SetNode(nodes[0], val, style)                          # 单节点总是写
            若 saveToArg0: (arguments[0] as VariableTerm).SetValue(doc.OuterXml, exm)
        返回 nodes.Count
```

## 备注

- 语义据源码；EM readme 的「第 4 参数为 0 或省略时对多个匹配结果不进行赋值」「第 5 参数 1=InnerText、2=InnerXml、其他=Value」与源码一致。
- **风格 0（默认）= 设置 `XmlNode.Value`**：对元素节点该属性在 .NET 里没有可写内容（元素没有值），因此对 `//elem` 这类选择结果，默认风格可能无效或抛异常；**要写元素文本请显式用风格 `1`（`InnerText`）或 `2`（`InnerXml`）**。风格 0 实际适用于属性节点（如 XPath `//@attr`）与文本节点。这一点属 .NET `XmlNode.Value` 语义推得（标注为推定），源码只是机械地调用 setter，未做节点类型判断（`SetNode` 在 `:810`）。
- 第 1 参数为字符串变量时**回写变量**（`saveToArg0`），回写内容始终是 `doc.OuterXml`（整篇文档，不是被改的节点）。因此「只改一个节点」也会把整篇文本规范化重写（自闭合、引号等格式可能变化）。
- 第 1 参数为整数（文档 ID）时**不回写**任何变量，`saveToArg0 = false`；`XML_GET_BYNAME` 的 byname 判定逻辑在此处是「字符串也当 ID」。
- 多节点 + `setall = 0` 时返回匹配数但一个都不改，容易误以为失败；单节点时不受 `setall` 影响。
- XPath 无匹配时返回 `0`，且**不写回变量**（`if (nodes.Count > 0)` 包住了回写）。
- 与 `XML_ADDNODE`/`XML_REPLACE` 不同，本函数没有「插入失败返回 0」的分支，只有 `-1`（文档不存在）与匹配数两种返回。
