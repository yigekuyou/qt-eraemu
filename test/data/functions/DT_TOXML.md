# DT_TOXML

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：str DT_TOXML(str 数据表名{, ref str 模式输出变量})
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

把指定数据表序列化成 XML 字符串并返回**数据 XML**；同时把**架构（schema）XML** 写入第 2 个参数（省略时写入 `RESULTS:1`）。表不存在返回 `""`（且不写输出参数）。

- 两步都由 .NET `System.Data.DataTable` 完成（`Runtime/Script/Statements/Function/Creator.Method.cs:1737-1741`）：
  1. `dt.WriteXmlSchema(sw)` → 架构 XML（含列定义、类型、主键约束）→ 写入输出参数；
  2. `dt.WriteXml(sw)` → 数据 XML → 作为返回值。
- 输出参数的落点由实参个数决定：
  - 省略第 2 参数 → 写 `RESULTS:1`（`RESULTS:0` **不动**；这是与其它 DT 函数「写 RESULTS:0 起」不同的地方，源码用 `idx = 1`）；
  - 给出第 2 参数 → 必须是一个**字符串变量**（`#DIMS` 声明的一维数组即可，长度 1 足够：源码取该变量底层数组并写下标 0，故「标量」与「长度 1 的一维数组」在实现上等价）。
- 与 `DT_FROMXML` 配对使用：`DT_FROMXML` 需要「架构 XML + 数据 XML」两个字符串，正好是本函数的两份产物（返回值给数据、输出参数给架构）。
- 只读操作，不修改表。

## 用法

### str DT_TOXML(str 数据表名)
```erb
DT_CREATE "t"
DT_COLUMN_ADD "t", "name", "string"
DT_ROW_ADD "t", "name", "甲"
PRINTSL DT_TOXML("t")            ; 打印数据 XML
PRINTSL RESULTS:1                ; 打印架构 XML（省略第 2 参时写这里）
PRINTFORML [{DT_TOXML("no_such")}]   ;→ []（表不存在）
```

### str DT_TOXML(str 数据表名, ref str 模式输出变量)
```erb
#DIMS SCHEMA              ; 字符串变量（长度 1 的一维数组即可）
#DIMS DATA
DATA = DT_TOXML("t", SCHEMA)  ; DATA = 数据 XML，SCHEMA = 架构 XML
DT_RELEASE "t"
DT_FROMXML "t", SCHEMA, DATA  ;→ 1（往返复原）
PRINTFORML {DT_CELL_GETS("t", 0, "name")}   ;→ 甲
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:305`（`["DT_TOXML"] = new DataTableToXmlMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1715`（`DataTableToXmlMethod`）

```text
构造（Creator.Method.cs:1717-1724）:
    返回类型 = typeof(string)
    argumentTypeArrayEx = [{ String, RefString }, OmitStart = 1]
    ; 第 2 参必须是字符串变量（RefString，维度 0），可省略
    CanRestructure = false

GetStrValue(exm, args)（Creator.Method.cs:1725-1743）:
    key = args[0]；dict = ...DataDataTables
    if !dict.ContainsKey(key): return ""                  ; 表不存在
    dt = dict[key]
    output = (args.Count > 1)
             ? (args[1] as VariableTerm).Identifier.GetArray() as string[]
             : GlobalStatic.VEvaluator.RESULTS_ARRAY       ; RESULTS 系统数组
    idx = (args.Count > 1) ? 0 : 1                         ; 省略时写 RESULTS:1
    sb = new StringBuilder()
    using sw = new StringWriter(sb):
        dt.WriteXmlSchema(sw)                              ; 架构 XML
        output[idx] = sb.ToString()
        sb.Clear()
        dt.WriteXml(sw)                                    ; 数据 XML
        return sb.ToString()                               ; 返回数据 XML
```

## 备注

- 无既有文档可对照。
- 命名容易误解：函数名只提 XML，但实际有「两个产物」——返回值是数据、输出参数才是架构；`DT_FROMXML` 需要两个都要，故单靠 `DT_TOXML` 的返回值**不足以**还原表（列类型与主键信息在架构里）。
- 省略第 2 参数时写 `RESULTS:1`（不是 `RESULTS:0`），若调用方之前用 `RESULTS` 存过别的内容要注意被覆盖。
- 对应 MAP 系的 `MAP_TOXML`（`Emuera.EM_readme.txt:266`，返回 `<map><p><k>..</k><v>..</v></p>...</map>`），但 MAP 版没有架构、没有输出参数。
