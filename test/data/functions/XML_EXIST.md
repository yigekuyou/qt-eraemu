# XML_EXIST

- **类别**：式中函数（EM 扩展 / XML 文档表）
- **签名**：`int XML_EXIST(<ID>)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:147-148` 有记载（「◆ int XML_EXIST id id(整数型)に対応するXmlDocumentが存在してい場合1を返す，そうでない場合0を返す」），`Emuera.EM_changelog.txt:29`（v2）追加。语义以源码为准。

## 语义

检查 XML 文档表中是否存在指定 ID 的文档：存在返回 `1`，不存在返回 `0`。ID 可以是整数也可以是字符串（参数类型 `Any`，内部统一转成字符串键）。

它只判断文档是否存在；文档内容为空/无子节点仍算「存在」。其余 `XML_*` 函数在文档不存在时的失败值是 `-1`，而本函数用 `0` 表示不存在，因此要区分「文档不存在」与「匹配到 0 个节点」时应该先调用本函数。

## 用法

### int XML_EXIST(ID)
- ID：整数或字符串表达式。
- 返回值：存在 `1`；不存在 `0`。
```erb
PRINTL XML_EXIST(0)                     ; 0（还没建立）

XML_DOCUMENT(0, "<root/>")
PRINTL XML_EXIST(0)                     ; 1

XML_RELEASE 0
PRINTL XML_EXIST(0)                     ; 0
```

```erb
; 惰性初始化
IF !XML_EXIST("cache")
    XML_DOCUMENT("cache", "<root/>")
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:252`（`["XML_EXIST"] = new XmlDocumentMethod(XmlDocumentMethod.Operation.Check)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:739`（`XmlDocumentMethod`，`Check` 分支在 `:783`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlDocumentMethod（op = Check）:
    构造:
        ReturnType = long
        op == Create: [ { Any, String } ]
        op != Create: [ { Any } ]            # 恰好 1 个参数
        CanRestructure = false
    GetIntValue(exm, arguments):
        idx = (arguments[0] 是整数) ? arguments[0].ToString() : arguments[0] 的字符串值
        xmlDict = exm.VEvaluator.VariableData.DataXmlDocument
        若 op == Create: （见 XML_DOCUMENT）
        否则:
            若 xmlDict.ContainsKey(idx):
                若 op == Check: 返回 1        # 本函数
                xmlDict.Remove(idx)           # Release
            否则: 返回 0
        返回 1
```

## 备注

- 语义据源码；EM readme 的「存在 1／不存在 0」与源码一致。readme 把 ID 写作「整数型」，源码参数类型是 `Any`（字符串 ID 同样可用），本文按源码记录。
- ID 的字符串化规则：整数按十进制 `ToString()`，因此 `XML_EXIST(1)` 与 `XML_EXIST("1")` 结果一致；`XML_EXIST("01")` 与 `XML_EXIST(1)` 是**不同**的槽位。
- 与 `MAP_EXIST` 类似，本函数是 XML 族里唯一以 `0` 表示「不存在」的检查函数；其余操作类函数的失败值是 `-1`。
- 文档表的键比较是 `Dictionary` 的默认字符串比较（区分大小写）。
