# 移植差异 · 式中函数

> 由 `test/data/functions/*.md` 的「备注」迁出（2026-10）。规则见 [`README.md`](README.md)。

## 核对口径（2026-10-08）

旧记录将 12 个单项和 DT/ENUM 家族统称“未实现”，与当前注册不符，本轮逐名只读核对
`src/eraengine` 的核心表、扩展注册及回调。下文“实现入口已存在”仅确认注册和处理代码，
不表示与 C# 完全语义等价；“登记桩”指只有名字登记；“未找到”指本轮未见内建注册或入口，
不推断所有调用场景必定产生哪一种诊断或返回值。没有运行引擎测试。

`GameProc/extension_registry.h` 构造函数调用 EE/Fork 注册；`regExpr` 登记式中函数及回调，
`reg(name, callback)` 仅登记语句处理入口，两者不能混为一谈。

## 实现入口已存在（式中函数，39 条）

### DT_*（21 条）

`DT_CREATE`、`DT_EXIST`、`DT_RELEASE`、`DT_CLEAR`、`DT_NOCASE`、`DT_FROMXML`、`DT_TOXML`、
`DT_SELECT`、`DT_CELL_GET`、`DT_CELL_GETS`、`DT_CELL_SET`、`DT_CELL_ISNULL`、`DT_COLUMN_ADD`、
`DT_COLUMN_EXIST`、`DT_COLUMN_LENGTH`、`DT_COLUMN_NAMES`、`DT_COLUMN_REMOVE`、`DT_ROW_ADD`、
`DT_ROW_SET`、`DT_ROW_REMOVE`、`DT_ROW_LENGTH`。

以上逐名存在于 `GameProc/fork_datatable.cpp` 的 `registerDataTableExtensions`，均以
`ext.regExpr` 登记并提供回调；`registerForkExtensions` 调用该注册入口。
该文件明确使用自有 XML 形状，因此不能据入口存在声称兼容 .NET DataTable XML。
另有语句形态 `DT_COLUMN_OPTIONS`，见 [`commands.md`](commands.md)。

### ENUM*（10 条）

`ENUMFILES`、`ENUMFUNCBEGINSWITH`、`ENUMFUNCENDSWITH`、`ENUMFUNCWITH`、
`ENUMMACROBEGINSWITH`、`ENUMMACROENDSWITH`、`ENUMMACROWITH`、`ENUMVARBEGINSWITH`、
`ENUMVARENDSWITH`、`ENUMVARWITH`。

`GameProc/fork_extension.cpp` 的 `registerForkExtensions` 直接以 `regExpr` 登记
`ENUMFILES`；其余 9 个由 `registerEnumFamily` 将 `ENUMFUNC`/`ENUMMACRO`/`ENUMVAR`
与 `BEGINSWITH`/`ENDSWITH`/`WITH` 组合后逐个登记回调。名字集合与目录服务由宿主注入，
不能仅凭注册推断所有环境下的结果均与 C# 相同。

### FLOWINPUTS（1 条）

`GameProc/ee_extension.cpp` 的 `registerEeExpressionFunctions` 以 `regExpr` 登记
`FLOWINPUTS`，回调调用 `setFlowInputString` 并返回 0。已存在服务调用入口，不能继续列为未实现。

### CSV 角色模板函数（7 条）

`CSVCFLAG`、`CSVEQUIP`、`CSVEXP`、`CSVJUEL`、`CSVMARK`、`CSVRELATION`、`CSVTALENT`
均在 `GameData/ast/function_types.h` 核心表中登记为 `BuiltinOp::CsvChara`，
由 `GameData/ast/expression_evaluator.cpp` 的对应分支读取角色 CSV 模板。

历史迁入记录称 example 的 `Chara0.csv` 未建对应模板数组、冒烟出现 `arg-check`/越界提示；
这是旧测试环境记录，本轮未重跑。`test/data/doc_smoke.tsv` 有这 7 个调用用例，
用例的存在不等于当前执行结果已验证。

## 未找到注册或实现入口（12 条）

`ERDNAME`、`EXISTFILE`、`EXISTMETH`、`EXISTVAR`、`GETMETH`、`GETMETHS`、`GETVAR`、`GETVARS`、
`BITMAP_CACHE_ENABLE`、`GGETBRUSH`、`GGETFONTSTYLE`、`ARRAYMSORTEX`。

以上逐名检索 `src/eraengine`（排除测试目录），未找到对应名字；同时核对核心函数表与扩展注册。
这 12 项由旧单项列表中移除已有入口的 `FLOWINPUTS`，再合入原“其它”中的 `ARRAYMSORTEX` 得出。
`ARRAYMSORTEX` 的 C# EM 扩展语义仍见 `test/data/functions/ARRAYMSORTEX.md`。
本页核对的式中函数中未发现只有名字登记的桩；语句登记不能代替式中函数注册。

## 同名语句入口已存在，式中函数注册未找到（3 条）

`FIND_VARDATA`、`CHKVARDATA`、`CHKGLOBALDATA` 在 `GameProc/ee_extension.cpp` 中
用 `ext.reg(name, callback)` 分别接入 `findVarData`、`chkVarData`、`chkGlobalData`。
这是语句处理实现，不能沿用原来的“式中函数已实现”结论；本轮未找到这三个名字的
`regExpr` 登记或核心式中函数表项。

## 历史计数校正

原“未实现函数族（32 条）”实际列出 DT 21 条 + ENUM 10 条，共 **31 条**。
原未实现名单为单项 12 + 家族 31 + `ARRAYMSORTEX` 1，共 **44 个名字**，
不是当前未实现数量。本页总计核对 54 个名字：39 个式中函数入口、12 个未找到、
3 个仅发现同名语句入口；没有据此声明完整 C# 语义等价。
