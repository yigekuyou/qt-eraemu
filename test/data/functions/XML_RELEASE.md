# XML_RELEASE

- **类别**：式中函数（EM 扩展 / XML 文档表）
- **签名**：`int XML_RELEASE(<ID>)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:144-145` 有记载（「◆ int XML_RELEASE id id(整数型)に対応するXmlDocumentを削除する。1を返す」），`Emuera.EM_changelog.txt:29`（v2）追加。语义以源码为准。

## 语义

从 XML 文档表中删除指定 ID 的文档（连同其全部内容）。ID 可以是整数也可以是字符串（参数类型 `Any`，内部统一转成字符串键）。

- 该 ID 存在并删除成功 → 返回 `1`。
- 该 ID 不存在 → 返回 `0`（与 `MAP_RELEASE` 恒返回 1 不同，本函数会如实报告失败）。

删除后该 ID 可以重新 `XML_DOCUMENT` 建立新文档。

## 用法

### int XML_RELEASE(ID)
- ID：整数或字符串表达式。
- 返回值：删除成功 `1`；该 ID 不存在 `0`。
```erb
XML_DOCUMENT(0, "<root/>")
PRINTL XML_RELEASE(0)      ; 1
PRINTL XML_RELEASE(0)      ; 0（已不存在）
PRINTL XML_EXIST(0)        ; 0
```

```erb
; 用「先释放再建立」实现整体覆盖
IF XML_EXIST(0)
    XML_RELEASE 0
ENDIF
XML_DOCUMENT(0, MAP_TOXML("data"))    ; 也可以直接放 MAP_TOXML 的结果
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:247`（`["XML_RELEASE"] = new XmlDocumentMethod(XmlDocumentMethod.Operation.Release)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:739`（`XmlDocumentMethod`，`Release` 分支在 `:784-786`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:19`（`DataXmlDocument`）

```text
XmlDocumentMethod（op = Release）:
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
                若 op == Check: 返回 1
                xmlDict.Remove(idx)          # 本函数：删除
            否则: 返回 0                     # 不存在 → 0（不再往下走）
        返回 1                               # 删除成功
```

## 备注

- 语义据源码；EM readme 只写了「删除并返回 1」，未提及「不存在时返回 0」——源码确实会返回 `0`（`:786` 的 `else return 0;`），如实记录。
- 与 `MAP_RELEASE` 的返回值约定不同（`MAP_RELEASE` 恒 `1`），与 `DT_RELEASE` 同类（后者未在本批内，未作对照）。
- 若该文档已由 `VarExt*.csv` 声明为随存档保存（`SAVE_XMLS`/`GLOBAL_XMLS`/`STATIC_XMLS`），删除后不再进入存档（保存时按集合逐个查找，找不到即跳过，`Runtime/Script/Statements/Variable/VariableData.cs:1001-1035`）。
- ID 的字符串化规则：整数按十进制 `ToString()`，`XML_RELEASE(1)` 与 `XML_RELEASE("1")` 等价；`"01"` 与 `1` 是不同槽位。
- 参数个数固定为 1；写两个参数会在解析期报错。
