# MAP_CREATE

- **类别**：式中函数（EM 扩展 / 字符串关联数组 map）
- **签名**：`int MAP_CREATE(str 名称)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:218-220` 有记载（「◆ int MAP_CREATE str 第一引数で指定した連想配列(Dictionary<string, string>)を作ります。1を返す すでに存在している場合0を返す」），`Emuera.EM_changelog.txt:23`（v3）追加。语义以源码为准。

## 语义

按名称创建一个「字符串关联数组」（内部类型 `Dictionary<string, string>`，本书简记作 map）。map 是存在变量数据（`VariableData.DataStringMaps`）里的全局命名字典，与角色无关；每个 map 从空开始，用 `MAP_SET` 写键值、`MAP_GET` 读值。

- 创建成功（此前不存在）→ 返回 `1`。
- 该名称的 map 已存在 → 返回 `0`，**不**清空原内容（不会覆盖）。

持久化：哪些 map 随存档/全局存档/静态（重启保留）保存，由 CSV 文件夹下的 `VarExt*.csv` 声明（`GLOBAL_MAPS`/`SAVE_MAPS`/`STATIC_MAPS`，见 `Runtime/Script/Data/ConstantData.cs:1281` 的 `loadGlobalVarExSetting`）；未声明的 map 不进入存档。

## 用法

### int MAP_CREATE(名称)
- 名称：map 的名字（字符串）。
- 返回值：新建成功 `1`；已存在 `0`。
```erb
IF MAP_CREATE("ITEM")
    PRINTL "新建 map ITEM"
ELSE
    PRINTL "map ITEM 已存在"
ENDIF

MAP_SET "ITEM", "药水", "回复 50 点"
PRINTL MAP_GET("ITEM", "药水")      ; 回复 50 点
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:265`（`["MAP_CREATE"] = new MapManagementMethod(MapManagementMethod.Operation.Create)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1780`（`MapManagementMethod`，`Create` 分支落在该方法的末尾 `:1801-1803`）
- 数据容器：`Runtime/Script/Statements/Variable/VariableData.cs:20`（`public Dictionary<string, Dictionary<string, string>> DataStringMaps`）

```text
MapManagementMethod（op = Create）:
    构造:
        ReturnType = long
        argumentTypeArray = [typeof(string)]        # 恰好 1 个字符串参数
        CanRestructure = false
    GetIntValue(exm, arguments):
        key = arguments[0] 的字符串值
        dict = exm.VEvaluator.VariableData.DataStringMaps
        contains = dict.ContainsKey(key)
        依 op 分流（本函数取 Create 之外的公共尾部）:
            Case/Release 分支不适用
            # Create: 落到函数末尾
        若 contains: 返回 0                          # 已存在，保留原内容
        dict[key] = new Dictionary<string, string>() # 新建空 map
        返回 1
```

## 备注

- 语义据源码；EM readme 的「新建返回 1、已存在返回 0」与源码一致。readme 未提到「已存在时保留原内容」——源码确认不清空（`if (contains) return 0;` 在 `dict[key] = []` 之前），此为源码细节补充。
- map 与 XML 文档（`XML_DOCUMENT`）、DataTable（`DT_CREATE`）三套容器共享同一套「名称 → 对象」的表，但各自独立：同名 map 与同名 XML 互不影响（分别存在 `DataStringMaps`、`DataXmlDocument` 中）。
- 名称可以是任意字符串（含空串）；源码不做合法性校验。
- 配套函数：`MAP_EXIST`（存在检查）、`MAP_RELEASE`（删除）、`MAP_CLEAR`（清空内容）、`MAP_SIZE`、`MAP_GET`/`MAP_SET`/`MAP_HAS`/`MAP_REMOVE`/`MAP_GETKEYS`/`MAP_TOXML`/`MAP_FROMXML`。
