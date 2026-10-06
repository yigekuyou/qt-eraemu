# SAVEDATA

- **类别**：命令
- **签名**：SAVEDATA `<数值表达式>`, `<字符串表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`（游戏存档的操作 一节，SAVEDATA 小节）；Era-Chinese-Documentation 仅在 Flow.md 中提及与 `CALL` 类似的返回行为，未单独收录

## 语义

把当前状态保存到第 1 参数所示编号的文件中。`SAVEDATA` 不会调用 `@SAVEINFO`，因此不能用 `PUTFORM` 写入注释，取而代之的是用第 2 参数的字符串式指定注释（从 1.704 起不仅可以传字符串变量，也可以传任意字符串表达式）。它不会进行覆盖确认等处理，如有需要请在 ERB 侧自行实现。是否已有数据可以用 `CHKDATA` 查询。与 `SAVEGAME` 不同，`SAVEDATA` 可以在脚本的任何位置调用。

错误行为：编号为负数或超过 int.MaxValue 抛 CodeEE；注释中含换行符抛 CodeEE（会导致存档损坏）；保存过程中发生其他意外错误时向控制台输出错误信息（不抛异常，`SaveTo` 返回 false）。

## 用法

### SAVEDATA `<数值表达式>`, `<字符串表达式>`
- `<数值表达式>`：存档编号（非负整数）。
- `<字符串表达式>`：写入存档的注释文本（不能包含换行符）。

```erb
GETTIME
STR:0 = %RESULTS:0% {DAY+1}日目
SAVEDATA 14, STR:0
SAVEDATA 15, RESULTS:0 + " " + @"{DAY+1}日目"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:228` → `argb[FunctionArgType.SP_SAVEDATA], METHOD_SAFE | EXTENDED`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:138`
- 参数解析：`Runtime/Script/Statements/ArgumentBuilder.cs:1542`（`SP_SAVEDATA_ArgumentBuilder`，类型序列 `[long, string]`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:323`（switch-case `FunctionCode.SAVEDATA`）；核心逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:2402`（`VariableEvaluator.SaveTo`）

```text
switch (func.Code) case SAVEDATA:
    spSavedataArg = (SpSaveDataArgument)func.Argument
    target = spSavedataArg.Target.GetIntValue(exm)          // 第 1 参数：存档编号
    if target < 0:
        throw CodeEE("SAVEDATA 的引数为负数")
    else if target > int.MaxValue:
        throw CodeEE("SAVEDATA 的引数过大")
    savemes = spSavedataArg.StrExpression.GetStrValue(exm)  // 第 2 参数：注释
    if savemes 含换行符 '\n':
        throw CodeEE("SAVEDATA 的保存文本中含换行符（会损坏存档）")
    if not vEvaluator.SaveTo((int)target, savemes):
        console.PrintError("SAVEDATA 保存中发生意外错误")    // 不抛异常，仅报告

VariableEvaluator.SaveTo(saveIndex, saveText):    // 返回 bool
    filepath = getSaveDataPath(saveIndex)
    try:
        Config.CreateSavDir()                     // 确保存档目录存在
        fs = 打开 filepath（Create/Write）
        if Config.SystemSaveInBinary:             // 按配置决定文本/二进制格式
            bWriter = EraBinaryDataWriter(fs)
            SaveToStreamBinary(bWriter, saveText)
        else:
            writer = EraDataWriter(fs)
            SaveToStream(writer, saveText)
        return true
    catch Exception:
        return false                              // 任何 IO/序列化错误都只返回 false
    finally:
        关闭 writer/bWriter/fs
```

## 备注

- 文档与源码一致：`SAVEDATA` 不调用 `@SAVEINFO`、不做覆盖确认；错误处理上只有编号非法与注释含换行是运行时异常，其余保存失败仅打印错误（对应 `Error_Index.md` 中的「SAVEDATA 命令によるセーブ中に予期しないエラー…」）。
- `SaveToStream`/`SaveToStreamBinary` 会序列化除局部变量外的全部游戏变量（含角色），是 `RESETDATA` 所初始化的那部分数据；与 `SAVECHARA` 的 `CharVar` 类型文件相区分。
- Era-Chinese-Documentation 的 Command.md 未单列本命令；Flow.md 提到 `BEGIN` 指令处时说明 `SAVEDATA`（与 `LOADDATA`）和 `CALL` 一样在完成后返回原处。
