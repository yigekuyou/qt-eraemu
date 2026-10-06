# MAP_FROMXML

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：`int MAP_FROMXML(str 名称, str xml)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:279-291` 有记载（「◆ int MAP_FROMXML str, str 第一引数で指定した連想配列に第二引数のXML文字列からキー-値ペアを読み取る。キーが存在している場合その値を上書きします。1を返す 連想配列自体が存在しない場合，0を返す」并规定 XML 必须是 `<map><p><k>キー</k><v>値</v></p>…</map>` 形式），`Emuera.EM_changelog.txt:23`（v3）追加。语义以源码为准。

## 语义

把 XML 字符串中的键值对读入指定 map。XML 的结构必须是：

```text
<map><p><k>键1</k><v>值1</v></p><p><k>键2</k><v>值2</v></p>…</map>
```

读取规则（源码）：以文档根为起点用 XPath `"/map/p"` 选出所有 `<p>`；对每个 `<p>`，再用 `./k` 与 `./v` 各取子节点，**只有当 `<k>` 与 `<v>` 各恰好 1 个时才处理**（其余 `<p>` 被跳过）；键取 `<k>` 的 `InnerText`，值取 `<v>` 的 `InnerXml`。键已存在则覆盖，不存在则新建。

- 读入完成 → 返回 `1`。
- 指定名称的 map 不存在 → 返回 `0`（**不自动创建**，注意这里的失败值是 `0` 而不是其他 map 函数的 `-1`）。
- XML 解析失败（`XmlException`）→ 抛 `CodeEE`（`"MAP_FROMXML関数:"…"の解析エラー:…"`）。

它是 `MAP_TOXML` 的逆操作：`MAP_FROMXML "M", MAP_TOXML("M")` 可自往返。

## 用法

### int MAP_FROMXML(名称, xml)
- 名称：map 的名字（字符串）；必须已由 `MAP_CREATE` 创建。
- xml：要读入的 XML 字符串。
- 返回值：成功 `1`；map 不存在 `0`；XML 非法抛 `CodeEE`。
```erb
; 往返：导出再导入
MAP_CREATE "A"
MAP_SET "A", "x", "1"
MAP_CREATE "B"
MAP_FROMXML "B", MAP_TOXML("A")
PRINTL MAP_GET("B", "x")          ; 1

PRINTL MAP_FROMXML("NOPE", "<map></map>")   ; 0（map 不存在）

; 手工构造
MAP_FROMXML "A", "<map><p><k>y</k><v>2</v></p></map>"
PRINTL MAP_GET("A", "y")          ; 2
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:278`（`["MAP_FROMXML"] = new MapFromXmlMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1930`（`MapFromXmlMethod`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`DataStringMaps`）

```text
MapFromXmlMethod:
    构造:
        ReturnType = long
        argumentTypeArray = [typeof(string), typeof(string)]     # 恰好 2 个字符串参数
        CanRestructure = false
    GetIntValue(exm, arguments):
        map = arguments[0] 的字符串值
        dict = exm.VEvaluator.VariableData.DataStringMaps
        若 !dict.ContainsKey(map): 返回 0                        # 不自动创建（注意是 0，不是 -1）
        xml = arguments[1] 的字符串值
        sMap = dict[map]
        doc = new XmlDocument()
        尝试:
            doc.LoadXml(xml)
            nodes = doc.SelectNodes("/map/p")                    # 绝对路径：根元素必须叫 map
        失败(XmlException) → 抛 CodeEE(XmlParseError, Name, xml, e.Message)
        对 i ∈ [0, nodes.Count):
            node = nodes[i]
            key = node.SelectNodes("./k")
            val = node.SelectNodes("./v")
            若 key.Count != 1 或 val.Count != 1: continue         # 结构不符的 <p> 跳过
            sMap[key[0].InnerText] = val[0].InnerXml              # 值取 InnerXml（保留内部标签原文）
        返回 1
```

## 备注

- 语义据源码；EM readme 的「覆盖已有键、返回 1、map 不存在返回 0、XML 必须为 `<map><p><k>/<v>` 形式」与源码一致。
- **失败返回值是 `0` 而非 `-1`**：这是 map 一族里的例外（`MAP_SET`/`MAP_HAS`/`MAP_REMOVE`/`MAP_CLEAR`/`MAP_SIZE` 在 map 不存在时返回 `-1`），readme 也写作 0，与源码一致，但作为「map 不存在」的标记容易与「成功读出 0 项」混淆——好在成功路径恒返回 1，故 `0` 唯一地表示「map 不存在」。
- 值取 `InnerXml` 而非 `InnerText`：`<v>` 内含标签时原样保留（与 `MAP_TOXML` 的写入侧对称）。
- 源码只捕获 `XmlException`；`SelectNodes` 的 `XPathException` 未被捕获，但因路径是写死的字面量 `"/map/p"`、`"./k"`、`"./v"`，不会触发（据源码推得，标注为推定）。
- `<p>` 内 `<k>`/`<v>` 多于或少于 1 个时该条被静默跳过，不报错、不计入结果。
- 键不含 XML 转义处理：键/值若来自 `MAP_TOXML` 且含 `&`、`<`，`LoadXml` 阶段就会解析失败（readme 也未提示这一限制）。
- 只做「读入 + 覆盖」，不会删除 map 中未被 XML 提及的旧键（即不是「整体替换」，而是「合并」）。
