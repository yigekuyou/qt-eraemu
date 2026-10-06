# DT_FROMXML

- **类别**：式中函数（EM 私家版扩展，DataTable 函数族）
- **签名**：int DT_FROMXML(str 数据表名, str 架构XML, str 数据XML)
- **文档来源**：两套中文文档与 EM/EE readme 均未收录，语义据源码

## 语义

用「架构 XML + 数据 XML」两份字符串重建一张数据表，存入指定名字下。成功返回 `1`，解析过程中任何异常都被吞掉并返回 `0`。

- 两份 XML 正是 `DT_TOXML` 的产物：架构来自其输出参数，数据来自其返回值（`DT_TOXML` 的文档有往返示例）。
- 两步解析（`Runtime/Script/Statements/Function/Creator.Method.cs:1760-1768`）：
  1. `new DataTable(key)` 建对象 → `ReadXmlSchema(架构XML)`；
  2. `ReadXml(数据XML)` 填数据。
  任一步抛异常（XML 格式错、结构不匹配等）→ `return 0`，**不写入字典**（原表若存在则保持原样，不会被破坏）。
- 成功后的写入是**覆盖式**：`dict[key] = dt`（已存在则替换），与 `DT_CREATE` 的「已存在返回 0」不同——本函数是「新建或整体替换」。
- 表的列、类型、主键约束都来自架构 XML；因此 `id` 列与主键是否存在，取决于导出时的表结构，而不是本函数自动补（与 `DT_CREATE` 不同）。
- 解析失败返回 0 时没有错误信息（异常被 `catch {}` 吞掉），也无法从返回值区分「格式错」与「结构不匹配」。

## 用法

### int DT_FROMXML(str 数据表名, str 架构XML, str 数据XML)
- 数据表名：目标键名（可与导出时不同，表内数据不受影响）。
- 架构XML：`DT_TOXML` 输出的 schema（列/类型/主键）。
- 数据XML：`DT_TOXML` 返回的数据 XML。
- 返回值：成功 `1`；解析失败 `0`。
```erb
#DIMS SCHEMA
#DIMS DATA
; 假设此前已 DT_CREATE "src" 等建表并写数据
DATA = DT_TOXML("src", SCHEMA)

PRINTFORML {DT_FROMXML("copy", SCHEMA, DATA)}    ;→ 1
PRINTFORML {DT_ROW_LENGTH("copy")}               ;→ 与原表相同
PRINTFORML {DT_CELL_GETS("copy", 0, "name")}     ;→ 同原表第 0 行

PRINTFORML {DT_FROMXML("bad", SCHEMA, "这不是XML")}   ;→ 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:306`（`["DT_FROMXML"] = new DataTableFromXmlMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1745`（`DataTableFromXmlMethod`）

```text
构造（Creator.Method.cs:1747-1752）:
    返回类型 = long
    argumentTypeArray = [string, string, string]     ; 三个参数都必填
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1753-1777）:
    key = args[0]；dict = ...DataDataTables
    try:
        dt = new DataTable(key)
        using reader = new StringReader(args[1] 的字符串值):
            dt.ReadXmlSchema(reader)                 ; 架构
        using reader = new StringReader(args[2] 的字符串值):
            dt.ReadXml(reader)                       ; 数据
    catch:
        return 0                                     ; 任何异常 → 0（原表不受影响）
    if dict.ContainsKey(key): dict[key] = dt         ; 覆盖已有的同名表
    else:                     dict.Add(key, dt)
    return 1
```

## 备注

- 无既有文档可对照。
- 与 `DT_CREATE` 的语义分工：`DT_CREATE` 只建空表并保证有 `id` 主键；`DT_FROMXML` 完全按 XML 还原结构，不保证有 `id` 列，也不保证主键存在。缺主键时 `DT_ROW_SET`/`DT_CELL_SET(…, 按id=1)`/`DT_ROW_REMOVE(单行)` 依赖的 `Rows.Find` 会抛 `MissingPrimaryKeyException`（推定：.NET 行为，非 CodeEE）。
- 成功后 `CaseSensitive` 等表属性由 XML 架构决定（`DT_TOXML` 的 schema 会带上这些设置，推定）。
- 与 MAP 系的 `MAP_FROMXML`（`Emuera.EM_readme.txt:279`）对照：MAP 版要求固定 `<map><p><k>..</k><v>..</v></p></map>` 结构、表不存在返回 0（只有已存在的 map 才写入）；DT 版则是「新建或替换」，语义更宽松。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
