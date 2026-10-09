# SAVENOS

- **类别**：命令（另存在同名式中函数，见备注）
- **签名**：`SAVENOS <数值变量>`
- **文档来源**：`ecd/docs/translation/Command.md`（「游戏存档的操作」章节）；`Era-Chinese-Documentation/docs/` 未收录该命令小节

## 语义

读取配置项「显示的存档数量」（`emuera.config` 中的存档显示数）的值，并赋给指定的数值变量。默认值为 20。

参数是不可省略的数值变量（可以带数组下标，如 `SAVENOS A:1`），传入字面量或表达式会出错。

## 用法

### SAVENOS <数值变量>

```erb
SAVENOS SAVENOS_COUNT
PRINTFORML 可显示的存档数 = {SAVENOS_COUNT}
```

配套的式中函数 `SAVENOS()`（无参数）可直接在表达式中取同值：

```erb
PRINTFORML 可显示的存档数 = {SAVENOS()}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:403`（`argb[FunctionArgType.SP_GETINT], METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:560`（`Process.ScriptProc` 的 switch-case `FunctionCode.SAVENOS`）

```text
case SAVENOS:                    // Process.ScriptProc.cs
    spGetintArg = (SpGetIntArgument)func.Argument
    // 参数解析层(SP_GETINT)已保证第 1 参数是可赋值的数值变量
    spGetintArg.VarToken.SetValue(Config.SaveDataNos, exm)
    // 即：目标变量 = 配置「显示的存档数量」
```

## 备注

- 同名式中函数：注册于 `Runtime/Script/Statements/Function/Creator.cs:67`（`GetSaveNosMethod`），实现在 `Runtime/Script/Statements/Function/Creator.Method.cs:2869`，同样返回 `Config.SaveDataNos`。索引中两个条目均记作 SAVENOS。
- 源码注释（FunctionIdentifier.cs:403 附近）说明该指令复用 SP_GETINT 参数类型但「引数的规格不同」——普通 SP_GETINT 用于读取配置的指令族，SAVENOS 借用它实现「向变量写入」。
- 与 `PRINTCPERLINE`（`Runtime/Script/Process.ScriptProc.cs:554`）实现完全同构，仅读取的配置项不同。
- **无文件副作用**：两种形态都只读 `Config.SaveDataNos`（命令形态把它写入目标变量、式中函数形态把它当返回值），参考树 `emuera.em/Emuera/` 中没有任何 nos 相关落盘（全树无 `nos.dat` / `SaveNosToFile`）。配置值的读取与夹紧见 `Runtime/Config/Config.cs`（默认 20，范围 20～80）。
- `zh/` 文档套件未收录本命令，无从交叉核对。
