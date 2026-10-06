# XML_ADDATTRIBUTE_BYNAME

- **类别**：式中函数（EM 扩展 / XML 文档表，`XML_ADDATTRIBUTE` 的按名变体）
- **签名**：`int XML_ADDATTRIBUTE_BYNAME(str 文档名, str xpath, str 属性名{, str 属性值, int 添加方式, int 全部添加})`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）、EE readme、本分支 EM readme 均未收录 `_BYNAME` 变体（EM readme 只写了 `XML_ADDATTRIBUTE`，`Emuera.EM_readme.txt:199`）。语义据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:893` 的 `XmlAddNodeMethod`，注册时传 `byname: true` 与 `Operation.Attribute`），属无外部文档、纯按源码编写的条目。

## 语义

与 `XML_ADDATTRIBUTE` 同一实现，差别有两处：

1. 参数表只剩一种形态：`String, String, String, String, Int, Int`（`OmitStart = 3`，即 3~6 个参数）——**第 1 参数只能是字符串**，且一律被当作**文档表的名字**。
2. 没有「回写变量」的行为：添加只发生在文档表里，用 `XML_TOSTR(文档名)` 取回结果。

其余语义与 `XML_ADDATTRIBUTE` 相同：第 3 参数属性名、第 4 参数属性值（可省略 → 空串）、第 5 参数添加方式（`0` 追加到元素属性表末尾；`1`/`2` 以选中属性为参照插在前/后，要求 xpath 选中属性节点）、第 6 参数全部添加（多匹配时非 `0` 才逐个添加，且只有参数个数正好为 6 时才读取）；文档名不存在返回 `-1`。

## 用法

### int XML_ADDATTRIBUTE_BYNAME(文档名, xpath, 属性名{, 属性值, 添加方式, 全部添加})
- 文档名：文档表里的名字（字符串）。
- xpath：定位元素（方式 0）或属性（方式 1/2）。
- 属性名 / 属性值：新属性的名字与值（值可省略）。
- 添加方式：`0` 追加（默认）；`1` 插在选中属性之前；`2` 插在之后。
- 全部添加：匹配多个节点时，`0`/省略 → 一个都不加；非 `0` → 逐个添加。
- 返回值：匹配个数；单节点添加失败 `0`；文档不存在 `-1`。
```erb
XML_DOCUMENT("save1", "<root><a>x</a></root>")

XML_ADDATTRIBUTE_BYNAME "save1", "//a", "id", "5"
PRINTL XML_TOSTR("save1")                                     ; <root><a id="5">x</a></root>

; 以 //a/@id 为参照，插到它前面
XML_ADDATTRIBUTE_BYNAME "save1", "//a/@id", "n", "1", 1
PRINTL XML_TOSTR("save1")                                     ; <root><a n="1" id="5">x</a></root>

PRINTL XML_ADDATTRIBUTE_BYNAME("nope", "//a", "k", "v")        ; -1
```

```erb
; 多匹配必须显式给「全部添加」非 0
XML_DOCUMENT("list", "<r><i/><i/></r>")
XML_ADDATTRIBUTE_BYNAME "list", "//i", "k", "1", 0, 1
PRINTL XML_TOSTR("list")        ; <r><i k="1" /><i k="1" /></r>
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:261`（`["XML_ADDATTRIBUTE_BYNAME"] = new XmlAddNodeMethod(XmlAddNodeMethod.Operation.Attribute, true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:893`（`XmlAddNodeMethod`；`byName` 在 `:924`，构造覆写在 `:912`，`Insert` 的 `Attribute` 分支在 `:944-960`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlAddNodeMethod（op = Attribute, byname = true 的构造）:
    构造(op, byname):
        ReturnType = long
        byName = byname
        argumentTypeArrayEx = [
            { ArgTypes = { String, String, String, String, Int, Int }, OmitStart = 3 }   # ★ 只剩字符串形态，3~6 个参数
        ]
        CanRestructure = false

    Insert(node, child, method):        # Attribute 分支
        若 child 是 XmlAttribute:
            若 method > 0 且 node 不是 XmlAttribute: 返回 false
            method 0 → node.Attributes.Append(child)
            method 1/2 → attr.OwnerElement.Attributes.InsertBefore/InsertAfter(...)
            返回 true
        返回 false

    GetIntValue(exm, arguments):
        methodPos = 5
        method = (参数个数 >= 5) ? arguments[4] 的整数值 : 0；越界夹回 0
        saveToArg0 = true
        若 arguments[0] 是整数 或 (byName 且 arguments[0] 是字符串):     # ★ 字符串一律当文档名
            saveToArg0 = false
            idx = arguments[0] 的字符串值；文档表含 idx ? doc = 表[idx] : 返回 -1
        否则: （本变体不可达）
        path = arguments[1]；SelectNodes 失败 → CodeEE(XmlXPathParseError, ...)
        若 nodes.Count > 0:
            setAllNodes = (参数个数 == 6) ? arguments[5] != 0 : false
            child = doc.CreateAttribute(arguments[2])
            若 参数个数 >= 4: child.Value = arguments[3]
            若 nodes.Count != 1: 若 setAllNodes: 逐个 Insert
            否则: 若 !Insert(nodes[0], child, method) 且 method > 0: 返回 0
            若 saveToArg0: 回写（本变体恒 false，不执行）
        返回 nodes.Count
```

## 备注

- **无外部文档**：`XML_ADDATTRIBUTE_BYNAME` 未出现在两套中文文档、EE readme、EM readme 中；本文据源码编写。与 `XML_ADDATTRIBUTE` 的差异即「字符串第 1 参数 = 文档名」与「没有 `RefString` 形态」两点。
- 参数位置：**添加方式是第 5 个、全部添加是第 6 个**；全部添加只在参数个数正好为 6 时读取。
- 方式 `1`/`2` 要求 xpath 选中属性节点（`@` 轴）；选中元素时失败（单匹配返回 `0`，多匹配跳过）。
- 多匹配共用同一个属性对象，实际只有最后一次插入生效（据 .NET DOM 语义推得，标注为推定）。
- 文档名区分大小写（`Dictionary` 默认 `Ordinal` 比较）；字符串形态不支持整数 ID，需要整数 ID 请用 `XML_ADDATTRIBUTE`。
