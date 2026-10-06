# XML_DOCUMENT

- **类别**：式中函数（EM 扩展 / XML 文档表）
- **签名**：`int XML_DOCUMENT(<ID>, str xml)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:140-142` 有记载（「◆ int XML_DOCUMENT id, xml xmlを解析し、XmlDocumentで保存する。1を返す id(整数型)に対応するXmlDocumentがすでに存在している場合，0を返す」），`Emuera.EM_changelog.txt:29`（v2）追加。语义以源码为准。

## 语义

解析一段 XML 文本，并以给定 ID 为名存入引擎内的「XML 文档表」（`VariableData.DataXmlDocument`，类型 `Dictionary<string, XmlDocument>`）。表里的文档是全局的，可用其余 `XML_*` 函数按 ID 直接做 XPath 查询/改写，不必每次重新解析文本。

- 存入成功 → 返回 `1`。
- 该 ID 已存在 → 返回 `0`，**旧文档保持不变**（不覆盖）。
- XML 解析失败（`XmlException`）→ 抛 `CodeEE`（`XML_DOCUMENT関数:"…"の解析エラー:…`）。

**ID 可以是整数也可以是字符串**（参数类型是 `Any`）：内部把 ID 统一转成字符串作键，`XML_DOCUMENT(1, ...)` 与 `XML_DOCUMENT("1", ...)` 指向同一槽位。因此字符串 ID 实际上比文档写的「整数 ID」更自由，也与 `XML_*_BYNAME` 系列共用同一张表。

持久化：哪些文档随存档/全局存档/静态保留，由 CSV 文件夹下的 `VarExt*.csv` 声明（`SAVE_XMLS`/`GLOBAL_XMLS`/`STATIC_XMLS`，见 `Runtime/Script/Data/ConstantData.cs:1281` 的 `loadGlobalVarExSetting`）。

## 用法

### int XML_DOCUMENT(ID, xml)
- ID：整数或字符串表达式，作为该文档的名字。
- xml：XML 文本（字符串表达式）。
- 返回值：新建 `1`；已存在 `0`；XML 非法抛 `CodeEE`。
```erb
IF XML_DOCUMENT(0, "<root><a id=""1"">aaa</a><a id=""2"">bbb</a></root>")
    PRINTL "文档已建立"
ENDIF
PRINTL XML_GET(0, "//a[@id='1']")        ; 1（匹配数）
PRINTL RESULTS:0                          ; aaa

XML_DOCUMENT(0, "<other/>")               ; 0（已存在，不覆盖）
```

```erb
; 字符串 ID 与整数 ID 等价
XML_DOCUMENT("save1", "<r><v>1</v></r>")
PRINTL XML_EXIST("save1")                 ; 1
PRINTL XML_TOSTR("save1")                 ; <r><v>1</v></r>
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:246`（`["XML_DOCUMENT"] = new XmlDocumentMethod(XmlDocumentMethod.Operation.Create)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:739`（`XmlDocumentMethod`，`Create` 分支在 `:761-778`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`public Dictionary<string, XmlDocument> DataXmlDocument`）

```text
XmlDocumentMethod（op = Create）:
    构造:
        ReturnType = long
        op == Create: argumentTypeArrayEx = [ { ArgTypes = { Any, String } } ]   # 恰好 2 个参数
        op != Create（Check/Release）: argumentTypeArrayEx = [ { ArgTypes = { Any } } ]   # 恰好 1 个参数
        CanRestructure = false
    GetIntValue(exm, arguments):
        idx = (arguments[0] 是整数) ? arguments[0].ToString()
                                   : arguments[0] 的字符串值        # 整数/字符串统一成字符串键
        xmlDict = exm.VEvaluator.VariableData.DataXmlDocument
        若 op == Create:
            xml = arguments[1] 的字符串值
            若 xmlDict.ContainsKey(idx): 返回 0                      # 已存在 → 不覆盖
            doc = new XmlDocument()
            尝试 doc.LoadXml(xml)
            失败(XmlException) → 抛 CodeEE(XmlGetError, xml, e.Message)
            xmlDict.Add(idx, doc)
        否则（Check/Release）:
            若 xmlDict.ContainsKey(idx):
                若 op == Check: 返回 1
                xmlDict.Remove(idx)                                  # Release
            否则: 返回 0                                             # 不存在
        返回 1
```

## 备注

- 语义据源码；EM readme 的「解析并保存返回 1、ID 已存在返回 0」与源码一致。readme 把 ID 写作「整数型」，而源码参数类型是 `Any`（整数与字符串都可），本文按源码记录。
- `Create` 分支的「已存在返回 0」意味着**想覆盖必须先 `XML_RELEASE`**（或改用 `XML_REPLACE` 的整文档替换形式）。
- 解析错误信息用 `XmlGetError`（字面量是「XML_GET関数:"{0}"の解析エラー:{1}」），即从 `XML_GET` 复制的消息模板：本函数解析失败时错误消息里会显示成 `XML_GET関数:` 开头。这是源码中的消息复用/笔误，如实记录（读源码得，非推定）。
- 与 map、DataTable 各自独立：`XML_DOCUMENT("A", ...)` 与 `MAP_CREATE "A"` 互不影响。
- 文档表是全局的、跨角色共享；`SAVEGAME` 时是否保存由 `VarExt*.csv` 的 `SAVE_XMLS`/`GLOBAL_XMLS`/`STATIC_XMLS` 声明（`Runtime/Script/Statements/Variable/VariableData.cs:1001-1035`）。
- 配套函数：`XML_EXIST`（存在检查）、`XML_RELEASE`（删除）、`XML_TOSTR`（取回文本）、`XML_GET`/`XML_SET`/`XML_ADDNODE`/`XML_REMOVENODE`/`XML_REPLACE`/`XML_ADDATTRIBUTE`/`XML_REMOVEATTRIBUTE` 及其 `_BYNAME` 变体。
