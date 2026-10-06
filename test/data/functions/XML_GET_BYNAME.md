# XML_GET_BYNAME

- **类别**：式中函数（EM 扩展 / XML 文档表，`XML_GET` 的按名变体）
- **签名**：
  - `int XML_GET_BYNAME(<xml>, str xpath)`
  - `int XML_GET_BYNAME(<xml>, str xpath, int 输出到 RESULTS{, int 输出风格})`
  - `int XML_GET_BYNAME(<xml>, str xpath, strArray 输出数组{, int 输出风格})`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）、EE readme、本分支 EM readme 均未收录 `_BYNAME` 变体（EM readme 只写了 `XML_GET`，`Emuera.EM_readme.txt:150`）。语义据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:54` 的 `XmlGetMethod`，注册时传 `byname: true`），是本批中无任何外部文档、纯按源码编写的条目之一。

## 语义

与 `XML_GET` 完全同一实现，唯一差别是**第 1 参数的字符串形态被判为「文档表的名字」而不是「XML 文本」**。即：

- 第 1 参数是字符串 → 作为键在文档表（`DataXmlDocument`）中查找（等价于 `XML_GET("键", ...)`，但 `XML_GET` 会把字符串当 XML 文本解析，本函数则当键）。
- 第 1 参数是整数 → 与 `XML_GET` 相同：整数转成字符串后当键查找。

因此本函数的价值是：**支持非数字的文档名**。`XML_DOCUMENT` 本来就能用字符串 ID（参数类型 `Any`），但 `XML_GET` 无法用字符串 ID 取回（会被当 XML 文本解析），所以要用本函数读取字符串命名的文档。

其余语义、参数、返回值、输出风格与 `XML_GET` 逐项相同（见 `XML_GET.md`），包括：文档不存在返回 `-1`、XPath 语法错误抛 `CodeEE`、第 3 参数给整数 `0` 会崩溃、风格 `4 = Name` 等。

## 用法

### int XML_GET_BYNAME(xml, xpath{, 输出目标, 输出风格})
- xml：文档表的名字（字符串）或整数 ID。
- xpath：XPath 表达式。
- 输出目标：非 `0` 整数 → `RESULTS`；一维字符串数组变量 → 该数组；省略 → 不输出。
- 输出风格：`1` InnerText / `2` InnerXml / `3` OuterXml / `4` Name / 其他 Value。
- 返回值：匹配个数；名字不存在 `-1`。
```erb
XML_DOCUMENT("char1", "<chara><name>Alice</name><lv>3</lv></chara>")

; 用字符串名直接读取（XML_GET 做不到这一点）
PRINTL XML_GET_BYNAME("char1", "//lv", 1, 1)    ; 1
PRINTL RESULTS:0                                ; 3

PRINTL XML_GET_BYNAME("nope", "//lv")           ; -1（文档不存在）
```

```erb
; 整数 ID 也可以（与 XML_GET 相同）
XML_DOCUMENT(0, "<root><a>x</a></root>")
PRINTL XML_GET_BYNAME(0, "//a", 1, 1)           ; 1
PRINTL RESULTS:0                                ; x
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:249`（`["XML_GET_BYNAME"] = new XmlGetMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:54`（`XmlGetMethod`，`byName` 字段在 `:73`，判定在 `:89`），节点输出辅助 `OutPutNode` 在 `:74`
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlGetMethod（byname = true 的构造）:
    构造(bool byname):
        ReturnType = long
        byName = byname
        argumentTypeArrayEx = [
            { ArgTypes = { Any, String, Int, Int }, OmitStart = 2 },
            { ArgTypes = { Any, String, RefString1D, Int }, OmitStart = 3 }
        ]                                     # 与 XML_GET 完全相同的参数表
        CanRestructure = false
    GetIntValue(exm, arguments):
        若 arguments[0] 是整数 或 (byName 且 arguments[0] 是字符串):     # ★ byName 让字符串也走这条路
            idx = arguments[0] 是字符串 ? 其字符串值 : 整数值.ToString()
            dict = exm.VEvaluator.VariableData.DataXmlDocument
            若 dict 含 idx: doc = dict[idx]
            否则: 返回 -1
        否则:
            doc = new XmlDocument()
            尝试 doc.LoadXml(arguments[0] 的字符串值)
            失败 → 抛 CodeEE(XmlGetError, xml, e.Message)
        path = arguments[1] 的字符串值
        尝试 nodes = doc.SelectNodes(path)
        失败(XPathException) → 抛 CodeEE(XmlGetPathError, path, e.Message)
        outputStyle = (参数个数 == 4) ? arguments[3] 的整数值 : 0
        若 参数个数 >= 3:
            若 arguments[2] 是整数 且 != 0: 写入 exm.VEvaluator.RESULTS_ARRAY（按 outputStyle）
            否则: arr = (arguments[2] as VariableTerm).Identifier.GetArray() as string[]; 写入 arr
        返回 nodes.Count
```

## 备注

- **无外部文档**：`XML_GET_BYNAME` 未出现在两套中文文档、EE readme、EM readme 中（EM readme 的 `XML_GET` 段落未提及 BYNAME 变体）。本文完全据源码编写；语义则是「`XML_GET` + 字符串第 1 参数当键」这一处差异，其余与 `XML_GET` 共享代码，风险低。
- 命名的含义：`byName` 让「字符串」被解释为**文档名**，与 `XML_DOCUMENT` 的字符串 ID 对应；因为有整数判断优先，本函数并不排斥整数 ID。
- 与 `XML_GET` 一样，第 1 参数为字符串时对应参数表的第 1 项是 `Any`，所以字面量（`XML_GET_BYNAME("doc", ...)`）合法；非 BYNAME 的 `XML_GET` 传字面量字符串则会被当 XML 文本解析并很可能抛解析错误。
- 文档名区分大小写（`Dictionary` 默认 `Ordinal` 比较）；`XML_DOCUMENT("Doc",...)` 之后 `XML_GET_BYNAME("doc",...)` 返回 `-1`。
- 其余注意事项（输出风格、`RESULTS` 截断、整数 `0` 崩溃、忽略数组下标）与 `XML_GET.md` 的备注完全相同，不再重复。
