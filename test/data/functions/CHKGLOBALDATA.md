# CHKGLOBALDATA

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（EE 计划扩展，本仓库未注册）
- **签名**：（未正式发布；按源码构造函数推断为 `int CHKGLOBALDATA(int value)`）
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 未收录；zh 套件未收录。语义依据源码中已实现但未注册的代码。

## 语义

按设计意图，本函数用于检查 `global.sav`（`SAVEGLOBAL`/`LOADGLOBAL` 保存的全局数据文件）是否存在、能否读取，返回与 `CHKDATA` 相同的状态码，并把结果信息写入 `RESULTS:0`、版本写入 `RESULT:1`：
- `0`：文件可读取；`1`：文件不存在；`2`：游戏代码不同；`3`：版本不同；`4`：其他问题。

**本仓库（emuera.em EE 版）中该函数未被注册**：注册处一行被注释掉（`//TODO:1810`），运行时调用 `CHKGLOBALDATA(...)` 会因函数未定义而解析失败。它与 `CHKVARDATA`、`FIND_VARDATA` 一样属于 Emuera 1.810 计划的变量数据文件检查函数，官方一直保留未启用。

## 用法

### （未注册，无可用用法）
按源码实现，参数为存档编号（数值表达式），但检查对象固定为 `global.sav`，编号本身不参与路径生成。
```erb
; 本仓库当前会报“函数不存在”类错误
;IF CHKGLOBALDATA(0) == 0
;    PRINTFORML global.sav 注释：{RESULTS:0}
;ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:59` — **被注释**（`//methodList["CHKGLOBALDATA"] = new CheckdataMethod(EraSaveFileType.Global);`）
- 实现：类本体与 `CHKDATA` 共用 `Runtime/Script/Statements/Function/Creator.Method.cs:2435`（`CheckdataMethod`）；检查逻辑转调 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1856`（`CheckData(int, EraSaveFileType)`），Global 分支下路径为 `getSaveDataPathG()`（固定 global.sav）

```text
函数 CHKGLOBALDATA(target):        ; 若注册，行为如下
    若 target < 0:            抛出 CodeEE（第 1 参数为负）
    若 target > int.MaxValue: 抛出 CodeEE（第 1 参数过大）
    result ← VEvaluator.CheckData((int)target, Global)
        filename ← getSaveDataPathG()     ; 固定为 global.sav，与 target 无关
        文件不存在 → (State=1, "----")
        校验游戏代码/版本/读取注释，状态码同 CHKDATA
    VEvaluator.RESULTS ← result.DataMes
    VEvaluator.RESULT_ARRAY[1] ← result.Version
    返回 (long)result.State
```

## 备注

- 三套文档均未收录；`Runtime/Script/Statements/BuiltInFunctionCode.cs:269` 中 `CHKVARDATA` 枚举同样被注释，可见该组函数整体处于「已实现、待启用」状态。
- 与 `CHKDATA` 的差异仅在 `EraSaveFileType.Global`：检查对象固定为 global.sav，参数编号无效；且 `LOADGLOBAL` 读取失败并不报错，因此即便启用，其用途也主要是读取前确认注释/版本。
- 注册行虽在索引中标为 `Runtime/Script/Statements/Function/Creator.cs:59 → CheckdataMethod`，但该行实为注释，文档生成时以实际源码为准。
