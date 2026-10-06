# XML_ADDATTRIBUTE

- **类别**：式中函数（EM 扩展 / XML 文档表）
- **签名**：
  - `int XML_ADDATTRIBUTE(<id>, str xpath, str 属性名{, str 属性值, int 添加方式, int 全部添加})`
  - `int XML_ADDATTRIBUTE(<xmlStrVar>, str xpath, str 属性名{, str 属性值, int 添加方式, int 全部添加})`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:199-206` 有记载（「◆ int XML_ADDATTRIBUTE xml, xpath, name(, value, addmethod, setall)」，含三种 addmethod 的定义与「value 可省略」），`Emuera.EM_changelog.txt:13`（v5）追加。语义以源码为准。

## 语义

用 XPath 选出节点（元素或属性），给它添加一个属性，返回匹配到的节点个数。与 `XML_ADDNODE` 是同一实现类（本函数为 `Operation.Attribute`）。第 1 参数：

- **整数**：文档表中的 ID，直接改写已保存的文档。
- **字符串变量**（`RefString`）：把变量内容当作 XML 文本解析，改写成功后**把结果写回该变量**。

第 3 参数是属性名，第 4 参数是属性值（**可省略**，省略时值为空串）。第 5 参数（添加方式，源码夹到 `0..2`）：

- `0` 或省略：追加到 xpath 选中**元素**的属性表末尾（`node.Attributes.Append`）。
- `1`：xpath 必须选中**属性节点**，把新属性插到它的**前面**（作为兄弟属性）；选中的是元素节点则失败。
- `2`：同上，插到**后面**。

第 6 参数（全部添加）：匹配到**多个**节点时，为 `0` 或省略则**一个都不加**；非 `0` 才逐个添加。匹配恰好 1 个节点时总是添加。

返回值：匹配到的节点个数；单节点添加失败（方式 1/2 选中的不是属性）返回 `0`；文档 ID 不存在返回 `-1`；XPath/XML 错误抛 `CodeEE`。无匹配时返回 `0`。

注意参数位置：`添加方式` 是**第 5 个参数**、`全部添加` 是**第 6 个参数**（与 `XML_ADDNODE` 的第 4/5 个不同），且 `全部添加` 只有在参数个数**正好为 6** 时才被读取。

## 用法

### int XML_ADDATTRIBUTE(id, xpath, 属性名{, 属性值, 添加方式, 全部添加})
```erb
XML_DOCUMENT(0, "<root><a>x</a></root>")

; 追加属性（值可省略）
PRINTL XML_ADDATTRIBUTE(0, "//a", "id")             ; 1
PRINTL XML_ADDATTRIBUTE(0, "//a", "lv", "3")        ; 1
PRINTL XML_TOSTR(0)                                  ; <root><a id="" lv="3">x</a></root>

; 以属性为参照插入兄弟属性（方式 1 = 插在前面）
PRINTL XML_ADDATTRIBUTE(0, "//a/@lv", "first", "y", 1)   ; 1
PRINTL XML_TOSTR(0)                                  ; <root><a id="" first="y" lv="3">x</a></root>

; 方式 1 但选中的是元素 → 失败
PRINTL XML_ADDATTRIBUTE(0, "//a", "z", "1", 1)      ; 0
```

### int XML_ADDATTRIBUTE(xmlStrVar, xpath, 属性名{, ...})
- 第 1 参数是**字符串变量**，添加后该变量被改写为结果文本。
```erb
#DIMS DOC = 4
DOC = "<r><i>x</i><i>y</i></r>"

; 多匹配 + 全部添加=0 → 不加
XML_ADDATTRIBUTE DOC, "//i", "k", "1", 0, 0
PRINTL DOC        ; <r><i>x</i><i>y</i></r>
XML_ADDATTRIBUTE DOC, "//i", "k", "1", 0, 1
PRINTL DOC        ; <r><i k="1">x</i><i k="1">y</i></r>
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:260`（`["XML_ADDATTRIBUTE"] = new XmlAddNodeMethod(XmlAddNodeMethod.Operation.Attribute)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:893`（`XmlAddNodeMethod`；`Insert` 的 `Attribute` 分支在 `:944-960`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlAddNodeMethod（op = Attribute）:
    构造:
        ReturnType = long
        argumentTypeArrayEx = [
            { ArgTypes = { Int, String, String, String, Int, Int }, OmitStart = 3 },        # 3~6 个参数
            { ArgTypes = { RefString, String, String, String, Int, Int }, OmitStart = 3 }
        ]
        CanRestructure = false

    Insert(node, child, method):        # :926，Attribute 分支（:944）
        若 child 是 XmlAttribute:
            若 method > 0 且 node 不是 XmlAttribute: 返回 false        # 方式 1/2 必须选属性
            attr = (method == 0) ? null : node as XmlAttribute
            method 0 → node.Attributes.Append(newAttr)
            method 1 → attr.OwnerElement.Attributes.InsertBefore(newAttr, attr)
            method 2 → attr.OwnerElement.Attributes.InsertAfter(newAttr, attr)
            返回 true
        返回 false

    GetIntValue(exm, arguments):
        methodPos = 5                                   # Attribute 时：添加方式在 arguments[4]
        method = (参数个数 >= 5) ? (int)arguments[4] 的整数值 : 0；越界夹回 0
        saveToArg0 = true
        若 arguments[0] 是整数 或 (byName 且 arguments[0] 是字符串):
            saveToArg0 = false
            idx = ...；文档表含 idx ? doc = 表[idx] : 返回 -1
        否则:
            doc = new XmlDocument(); doc.LoadXml(arguments[0] 的字符串值)
            失败 → 抛 CodeEE(XmlParseError, Name, xml, e.Message)
        path = arguments[1]；SelectNodes 失败 → 抛 CodeEE(XmlXPathParseError, ...)
        若 nodes.Count > 0:
            setAllPos = 6                               # Attribute 时：全部添加在 arguments[5]，且需正好 6 个参数
            setAllNodes = (参数个数 == 6) ? arguments[5] != 0 : false
            child = doc.CreateAttribute(arguments[2] 的字符串值)          # 属性名
            若 参数个数 >= 4: child.Value = arguments[3] 的字符串值        # 属性值（可省略 → 空串）
            若 nodes.Count != 1:
                若 setAllNodes: for i: Insert(nodes[i], child, method)     # 多匹配且 setall=0 → 不加
            否则:
                若 !Insert(nodes[0], child, method) 且 method > 0: 返回 0
            若 saveToArg0: (arguments[0] as VariableTerm).SetValue(doc.OuterXml, exm)
        返回 nodes.Count
```

## 备注

- 语义据源码；EM readme 的三种 addmethod 定义（0=追加到元素属性表末尾、1=插到选中属性之前、2=插到之后，且方式 1/2 时 value 可省略）与「setall 为 0 或省略时对多个匹配结果不添加」「成功返回匹配数、失败返回 0、文档不存在返回 -1」与源码一致。
- **参数位置容易记错**：`添加方式` 是第 5 个参数、`全部添加` 是第 6 个；而 `XML_ADDNODE` 里它们是第 4、5 个。`全部添加` 只在参数个数正好为 6 时生效，所以想给 `全部添加` 必须先给 `添加方式`。
- 第 4 参数（属性值）可省略，省略时属性值为空串（`doc.CreateAttribute(name)` 的默认值），源码用 `参数个数 >= 4` 判定。
- 方式 `1`/`2` 时 xpath 必须选中**属性节点**（`@` 轴）；选中元素时 `Insert` 返回 false，单匹配 → 返回 `0`，多匹配 → 该节点被跳过。
- 多匹配时**所有节点共用同一个 `child` 属性对象**（源码在循环外只创建一次），逐个 `Append`/`InsertBefore` 时后一次会把该属性从上一处移走（.NET DOM 对已有 owner 的属性执行移动）。因此「多匹配 + 全部添加」实际只有最后一次插入生效——据 .NET DOM 语义推得，标注为推定。
- 第 1 参数为字符串变量时回写整篇 `doc.OuterXml`；整数 ID 形式不回写。
- 参数表要求第 3、4 参数是 `String`（属性名/属性值），传整数会在解析期报错。
