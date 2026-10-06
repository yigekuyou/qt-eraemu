# XML_TOSTR

- **类别**：式中函数（EM 扩展 / XML 文档表）
- **签名**：`str XML_TOSTR(<ID>)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:172-174` 有记载（「◆ str XML_TOSTR int 第一引数で指定したXmlDocumentを文字列に変換して返す。保存したXmlDocumentが存在しない場合，空文字列を返す」），`Emuera.EM_changelog.txt:13`（v5 描述追加）、`:29`（v2 追加）。语义以源码为准。

## 语义

把文档表中指定 ID 的 XML 文档重新序列化成字符串（`XmlDocument.OuterXml`）返回，即「取回当前内容」。经过 `XML_SET`/`XML_ADDNODE`/`XML_REPLACE` 等改写后，本函数返回的是改写后的最新形态。

- 文档存在 → 返回其 `OuterXml`。
- 文档不存在 → 返回空串 `""`（不报错、不返回 -1）。

ID 可以是整数也可以是字符串（参数类型 `Any`，内部统一转成字符串键）。序列化结果由 .NET 的 `XmlDocument.OuterXml` 决定（属性顺序按文档顺序、空元素写作 `<a />`），与 `XML_DOCUMENT` 时传入的原始文本**不一定逐字相同**（例如自闭合写法、属性引号、实体转义会被规范化）。

## 用法

### str XML_TOSTR(ID)
- ID：整数或字符串表达式。
- 返回值：文档的 XML 文本；不存在时为空串。
```erb
XML_DOCUMENT(0, "<root><a>x</a></root>")
PRINTL XML_TOSTR(0)              ; <root><a>x</a></root>
PRINTL XML_TOSTR("nope")         ; （空行）

; 改写后再取回
XML_SET 0, "//a", "y", 1, 1
PRINTL XML_TOSTR(0)              ; <root><a>y</a></root>
```

```erb
; 把改好的文档存回普通字符串变量，便于 SAVETEXT 落盘
S = XML_TOSTR(0)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:253`（`["XML_TOSTR"] = new XmlToStrMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:875`（`XmlToStrMethod`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlToStrMethod:
    构造:
        ReturnType = string
        argumentTypeArrayEx = [ { ArgTypes = { Any } } ]     # 恰好 1 个参数（整数或字符串）
        CanRestructure = false
    GetStrValue(exm, arguments):
        idx = (arguments[0] 是整数) ? arguments[0].ToString() : arguments[0] 的字符串值
        xmlDict = exm.VEvaluator.VariableData.DataXmlDocument
        若 !xmlDict.ContainsKey(idx): 返回 string.Empty      # 文档不存在 → 空串
        返回 xmlDict[idx].OuterXml                           # 含根元素的完整 XML 文本
```

## 备注

- 语义据源码；EM readme 的「转换为字符串返回、不存在时空字符串」与源码一致，readme 把 ID 写作 `int`，源码参数类型是 `Any`（字符串 ID 同样可用），本文按源码记录。
- 返回的是 `OuterXml`（含根元素），不是 `InnerXml`；`XML_GET` 的 style=3 也能取到同样的内容，但那是针对 XPath 选中的节点。
- 序列化会规范化格式：`<!DOCTYPE>`/XML 声明不会保留（`OuterXml` 不含声明）、属性值引号可能与原文不同、命名空间前缀按文档内部表示输出。原文与返回值不完全等价的情况属 .NET 序列化行为（据 .NET 语义推得，标注为推定，源码本身只做了一次 `OuterXml`）。
- 与 `MAP_TOXML` 无关：本函数只读 XML 文档表；map 的 XML 化用 `MAP_TOXML`。
- 参数个数固定为 1。
