# XML_GET

- **类别**：式中函数（EM 扩展 / XML 文档表）
- **签名**：
  - `int XML_GET(<xml>, str xpath)`
  - `int XML_GET(<xml>, str xpath, int 输出到 RESULTS{, int 输出风格})`
  - `int XML_GET(<xml>, str xpath, strArray 输出数组{, int 输出风格})`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:150-160` 有记载（「◆ int XML_GET xml, xpath(, strArray/int, int)」），`Emuera.EM_changelog.txt:11`（v5「XML_SET, XML_GETの説明文の修正」）、`:29-30`（v2 追加，且说明第 1 参数可为整数）。语义以源码为准。

## 语义

用 XPath 在 XML 上选择节点，返回**匹配到的节点个数**。第 1 参数有两种形态：

- **整数**：视为文档表中的 ID，取已保存的文档来查询；该 ID 不存在 → 返回 `-1`。
- **字符串变量**：把变量的内容当作 XML 文本现场解析（每次调用都重新解析，不改写变量）。

第 2 参数是 XPath 表达式（`XmlDocument.SelectNodes`），语法错误 → 抛 `CodeEE`（`XML_GET関数:XPath"…"の解析エラー:…`）。XML 文本解析失败 → 抛 `CodeEE`（`XML_GET関数:"…"の解析エラー:…`）。

结果的取出：

- 不给第 3 参数 → 只返回个数。
- 第 3 参数是**非 `0` 的整数** → 把节点内容依次写入 `RESULTS:0`、`RESULTS:1`……（写入个数受 `RESULTS` 数组长度限制）。**注意**：整数 `0` 会落入「当作字符串数组变量处理」的分支并因类型不符而崩溃（见备注）。
- 第 3 参数是**一维字符串数组变量** → 写入该数组（**忽略**数组名里写的下标，从头覆盖；受数组长度限制）。
- 第 4 参数（输出风格）：`1` = `InnerText`、`2` = `InnerXml`、`3` = `OuterXml`、`4` = `Name`、其他（含省略时的 `0`）= `Value`。

因为元素节点（`XmlElement`）的 `Value` 在 .NET 里是 `null`，风格 `0` 只对属性节点/文本节点有意义；**取元素内容请显式给风格 `1`（`InnerText`）或 `2`/`3`**。

## 用法

### int XML_GET(xml, xpath{, 输出目标, 输出风格})
- xml：整数（文档表 ID）或字符串变量（XML 文本）。
- xpath：XPath 表达式。
- 输出目标：非 `0` 整数 → 输出到 `RESULTS`；一维字符串数组变量 → 输出到该数组；省略 → 不输出。
- 输出风格：`1` InnerText / `2` InnerXml / `3` OuterXml / `4` Name / 其他 Value。
- 返回值：匹配个数；第 1 参数是文档 ID 且不存在时 `-1`。
```erb
XML_DOCUMENT(0, "<root><a id=""1"">aaa</a><a id=""2"">bbb</a></root>")

; 只数个数
PRINTL XML_GET(0, "//a")                 ; 2

; 输出到 RESULTS（风格 1 = InnerText）
XML_GET 0, "//a", 1, 1
PRINTL RESULT                            ; （RESULT 未被本函数改写）
PRINTL RESULTS:0                         ; aaa
PRINTL RESULTS:1                         ; bbb

; 输出到自己的数组（风格 3 = OuterXml）
#DIMS NODES = 8
XML_GET 0, "//a", NODES, 3
LOOP LOCAL, 0, 2
    PRINTFORML {NODES:LOCAL}
NEXT

; 第 1 参数给字符串变量 → 现场解析文本
#DIMS SRC = 4
SRC = "<r><v>hello</v></r>"
PRINTL XML_GET(SRC, "//v", 1, 1)         ; 1
PRINTL RESULTS:0                          ; hello
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:248`（`["XML_GET"] = new XmlGetMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:54`（`XmlGetMethod`，节点输出辅助 `OutPutNode` 在 `:74`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlGetMethod（无 byname 的构造）:
    构造:
        ReturnType = long
        argumentTypeArrayEx = [
            { ArgTypes = { Any, String, Int, Int }, OmitStart = 2 },        # 2~4 个参数（形式 1）
            { ArgTypes = { Any, String, RefString1D, Int }, OmitStart = 3 } # 3~4 个参数（形式 2）
        ]
        CanRestructure = false

    OutPutNode(node, array, i, style):          # :74
        style 1 → InnerText；2 → InnerXml；3 → OuterXml；4 → Name；其他 → Value

    GetIntValue(exm, arguments):
        doc = null; nodes = null
        若 arguments[0] 是整数 或 (byName 且 arguments[0] 是字符串):
            # —— 走文档表（BYNAME 变体额外允许用字符串做键）
            idx = arguments[0] 是字符串 ? 其字符串值 : 整数值.ToString()
            dict = exm.VEvaluator.VariableData.DataXmlDocument
            若 dict 含 idx: doc = dict[idx]
            否则: 返回 -1                                     # 文档不存在
        否则:
            # —— 现场解析 XML 文本（第 1 参数必须是字符串变量）
            doc = new XmlDocument()
            尝试 doc.LoadXml(arguments[0] 的字符串值)
            失败(XmlException) → 抛 CodeEE(XmlGetError, xml, e.Message)

        path = arguments[1] 的字符串值
        尝试 nodes = doc.SelectNodes(path)
        失败(XPathException) → 抛 CodeEE(XmlGetPathError, path, e.Message)

        outputStyle = (参数个数 == 4) ? arguments[3] 的整数值 : 0

        若 参数个数 >= 3:
            若 arguments[2] 是整数 且 其值 != 0:
                for i = 0 .. min(nodes.Count, RESULTS_ARRAY.Length)-1:
                    OutPutNode(nodes[i], RESULTS_ARRAY, i, outputStyle)     # 写入 RESULTS
            否则:
                arr = (arguments[2] as VariableTerm).Identifier.GetArray() as string[]
                                                                           # ★ 整数 0 会在这里崩溃
                for i = 0 .. min(nodes.Count, arr.Length)-1:
                    OutPutNode(nodes[i], arr, i, outputStyle)
        返回 nodes.Count
```

## 备注

- 语义据源码；EM readme 描述的第 1 参数「整数 → 用保存的文档，不存在返回 -1」「第 3 参数为字符串数组时结果写入该数组」「第 4 参数 1=InnerText, 2=InnerXml, 3=OuterXml, 其他=Value」与源码一致。readme 未记载风格值 `4`（`Name`）——那是源码额外支持的值（`OutPutNode` 的 `case 4`），如实记录。
- **第 3 参数给整数 `0` 会崩溃**：源码判断「是整数且非 0」才写 `RESULTS`，否则一律强转 `arguments[2] as VariableTerm`；整数 `0` 的字面量强转得到 `null`，随后 `.Identifier` 抛 `NullReferenceException`。因此「想只看个数」必须**省略第 3 参数**，不能写 `0`（此结论据源码分支结构推得，未运行验证，标注为推定）。
- 风格 `0`（默认）取的是 `XmlNode.Value`：对元素节点为 `null`（写入字符串数组后读到的是空串），只对属性/文本节点有意义。取元素内容请用 `1`/`2`/`3`（据 .NET `XmlNode.Value` 语义推得，标注为推定）。
- 第 3 参数为数组时**忽略变量名中写的下标**（`Identifier.GetArray()` 取整个数组），也不清理数组剩余元素。
- 第 1 参数为字符串时对应参数表要求的是 `RefString`（形式 1 的 `Any` 允许字面量，但那就必然被当作整数或 BYNAME 键；非 BYNAME 的字面量字符串无法通过「是整数」判断，会落到解析分支——不过形式 1 的第 1 参数是 `Any`，字面量字符串会走 `else` 分支被当 XML 解析，此时不可回写；`saveToArg0` 逻辑在 `XML_SET` 里，本函数无回写）。
- 与 `XML_GET_BYNAME` 的关系：同一实现类，`byName = true` 时字符串第 1 参数被当作**文档表键**而不是 XML 文本。
