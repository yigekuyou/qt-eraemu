# MAP_TOXML

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：`str MAP_TOXML(str 名称)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:266-277` 有记载（「◆ str MAP_TOXML str 第一引数で指定した連想配列をXML文字列に変換し，XML文字列を返す。連想配列自体が存在しない場合，空文字列を返す」并给出 `<map><p><k>キー1</k><v>値1</v></p>…</map>` 的格式），`Emuera.EM_changelog.txt:23`（v3）追加、v4「MAP_TOXMLバグ修正」。语义以源码为准。

## 语义

把整个 map 序列化成 XML 字符串。确切格式（源码 `string.Format` 逐字拼接，**无换行、无缩进**）：

```text
<map><p><k>键1</k><v>值1</v></p><p><k>键2</k><v>值2</v></p>…</map>
```

- map 存在 → 返回该 XML 字符串；空 map 返回 `"<map></map>"`。
- map 不存在 → 返回空串 `""`（与「空 map」的返回值不同，可据此区分，也可用 `MAP_EXIST`）。

键与值的迭代顺序是 `Dictionary` 内部顺序（与 `MAP_GETKEYS` 一致，源码不排序）。**键和值都未做 XML 转义**：含 `<`、`&`、`>` 等字符的键/值会直接插入，产出的字符串可能不是合法 XML，回读（`MAP_FROMXML`）时会解析失败或被截断。需要往返时应自行避免或自行转义这些字符。

## 用法

### str MAP_TOXML(名称)
- 名称：map 的名字（字符串）。
- 返回值：XML 字符串；map 不存在时为空串。
```erb
MAP_CREATE "ITEM"
MAP_SET "ITEM", "药水", "50"
MAP_SET "ITEM", "解毒药", "30"
PRINTL MAP_TOXML("ITEM")
; <map><p><k>药水</k><v>50</v></p><p><k>解毒药</k><v>30</v></p></map>

PRINTL MAP_TOXML("NOPE")      ; （空行：map 不存在）
```

```erb
; 存成字符串数组变量中的一个条目（配合 SAVETEXT 等落盘）
#DIMS DUMP = 4
DUMP:0 = MAP_TOXML("ITEM")
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:277`（`["MAP_TOXML"] = new MapGetStrMethod(MapGetStrMethod.Operation.ToXml)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1847`（`MapGetStrMethod`；`ToXml` 的参数表在 `:1857-1858`、逻辑在 `:1906-1927` 的 `StringBuilder` 分支）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`DataStringMaps`）

```text
MapGetStrMethod（op = ToXml）:
    构造:
        ReturnType = string
        switch (type):
            Get:    [typeof(string), typeof(string)]
            ToXml:  [typeof(string)]                    # 恰好 1 个参数
            GetKeys: argumentTypeArrayEx = [ {String, Int}, OmitStart = 1 } / { String, RefString1D, Int }
        CanRestructure = false
    GetStrValue(exm, arguments):
        dict = exm.VEvaluator.VariableData.DataStringMaps
        map = arguments[0] 的字符串值
        若 !dict.ContainsKey(map): 返回 ""               # map 不存在 → 空串
        sMap = dict[map]
        （op == Get / GetKeys 且参数个数 > 1 的分支不适用）
        sb = new StringBuilder()
        若 op == GetKeys: （逗号拼接，见 MAP_GETKEYS）
        否则（ToXml）:
            sb.Append("<map>")
            遍历 p ∈ sMap:
                sb.Append(string.Format("<p><k>{0}</k><v>{1}</v></p>", p.Key, p.Value))
            sb.Append("</map>")
        返回 sb.ToString()
```

## 备注

- 语义据源码；EM readme 给出的格式与源码完全一致（`<map>` 下的 `<p><k>…</k><v>…</v></p>`）。readme 的示例是缩进排版，**源码实际不产生任何换行/缩进**，如实记录这一外观差异（内容等价）。
- **不做 XML 转义**（源码直接 `string.Format` 拼接 `p.Key`/`p.Value`）：键或值含 `&`、`<`、`>` 时输出不是合法 XML；`MAP_FROMXML` 再用 `XmlDocument.LoadXml` 解析会抛 `CodeEE`。往返场景请避开这些字符。此为源码级限制，readme 未提及（标注为据源码推得）。
- 与 `MAP_FROMXML` 构成往返对：`MAP_TOXML` → `MAP_FROMXML` 能还原键值的**文本内容**；值的读取用 `InnerXml`（见 `MAP_FROMXML.md`）。
- 与 `XML_*` 族无耦合：`MAP_TOXML` 只是生成一段字符串，不会在 XML 文档表里创建任何对象；要把它变成可 XPath 操作的文档，需自己 `XML_DOCUMENT id, MAP_TOXML(...)`。
- 与 `MAP_GETKEYS` 共用迭代顺序（同一个 `sMap` 的枚举），所以两者的键序一致。
