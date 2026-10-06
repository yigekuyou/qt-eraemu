# SAVEGLOBAL

- **类别**：命令
- **签名**：`SAVEGLOBAL`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md`（「游戏存档的操作」章节）；`Era-Chinese-Documentation/docs/` 各文档未收录该命令小节

## 语义

把全局变量 `GLOBAL`、`GLOBALS` 保存到存档目录下的 `global.sav`。如果 ERH 文件中以 `GLOBAL` 与 `SAVEDATA` 标志声明的私有变量也会一并保存。

使用限制：无特别上下文限制（带 METHOD_SAFE | EXTENDED 标志）。保存失败（如磁盘/目录错误）时不静默，会抛出运行时错误终止脚本（这与读取侧 LOADGLOBAL「读取失败也不出错」不同）。

## 用法

### SAVEGLOBAL

无参数。

```erb
GLOBAL:0 = DAY
GLOBAL:1 = MONEY
SAVEGLOBAL
```

读取用 `LOADGLOBAL`，成功时 `RESULT` 为 1，失败为 0。

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:231`（`new SAVEGLOBAL_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1833`（`SAVEGLOBAL_Instruction`，`DoInstruction` 在 1841 行）；实际保存逻辑转调 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:2232`（`VariableEvaluator.SaveGlobal()`），序列化在 `Runtime/Script/Statements/Variable/VariableData.cs` 的 `SaveGlobalToStream` / `SaveGlobalToStream1808` / `SaveGlobalToStreamBinary`（922/934/1049 行）

```text
SAVEGLOBAL_Instruction:
    构造: 无参数(VOID), 标记 METHOD_SAFE | EXTENDED

    DoInstruction(exm, func, state):
        exm.VEvaluator.SaveGlobal()
        // 返回值被忽略（SaveGlobal 总是返回 true 或抛错）

VariableEvaluator.SaveGlobal():
    filepath = getSaveDataPathG()            // global.sav 的路径
    try:
        Config.CreateSavDir()                // 确保存档目录存在
        以 FileMode.Create 打开文件流
        if Config.SystemSaveInBinary:        // 二进制格式
            写文件头 / 文件类型(Global)
            写 gamebase.ScriptUniqueCode     // 游戏代码
            写 gamebase.ScriptVersion        // 版本
            写 ""                            // saveMes（空注释）
            varData.SaveGlobalToStreamBinary(bWriter)
            写 EOF
            varData.SaveGlobalEMDataToStreamBinary(bWriter)  // EM 扩展变量数据
            写 EOF
        else:                                // 旧文本格式
            写 gamebase.ScriptUniqueCode
            写 gamebase.ScriptVersion
            varData.SaveGlobalToStream(writer)
            writer.EmuStart()
            varData.SaveGlobalToStream1808(writer)
        return true
    catch SystemException:
        throw CodeEE("保存全局数据时发生错误")
```

## 备注

- 文档称「保存失败」未提及行为；源码中失败会抛 `CodeEE` 错误（`ErrorSavingGlobalData`），并非静默失败。
- 源码保存顺序：普通 `GLOBAL`/`GLOBALS` 先写，ERH 中 `GLOBAL` + `SAVEDATA` 标志的 EM 扩展变量随后追加（有源码注释说明这是对「EM 的数据原本未被 SAVEGLOBAL 保存」的修正）。
- 保存的文件头包含 `gamebase.csv` 的代码与版本；`LOADGLOBAL` 读取时会校验，不匹配则视为读取失败。
- 保存格式由配置 `SystemSaveInBinary` 决定（二进制 / 旧文本 1808 兼容格式）。
