# OUTPUTLOG

- **类别**：命令（本仓库实际实现为式中函数）
- **签名**：
  - `OUTPUTLOG`（ecd 文档记载的命令形式）
  - `OUTPUTLOG(<文件名>{, <是否省略环境信息>})`（本仓库实际提供的式中函数形式）
- **文档来源**：`ecd/docs/translation/Command.md`「存档·读取」相关小节（`### OUTPUTLOG`）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

把当前控制台日志输出到日志文件。文档记载的命令形式是输出到 `emuera.log`，日志文字编码为 Unicode；并提醒过度使用会缩短磁盘寿命。

本仓库（EE 变体）中，命令形式的 `OUTPUTLOG` 已被停用（枚举、注册与 switch 分支全部被注释），改为提供同名的式中函数：可指定输出文件名（相对 Emuera 可执行文件目录，不能为空、不能包含 `../`、只能输出到该目录及其子目录内），可指定是否省略文件头部环境信息（版本、变体、Gamebase 信息等）。文件已存在时被覆盖。函数形式返回值恒为 1。此外 EE 在脚本解析出错退出等时机也会自动调用系统日志输出（`OutputSystemLog`）。

## 用法

### `OUTPUTLOG`（文档记载的命令形式；本仓库不可用）
```erb
;原命令形式：把当前日志输出到 emuera.log
OUTPUTLOG
```
### `OUTPUTLOG(<文件名>{, <是否省略环境信息>})`（本仓库实际可用的式中函数）
- `<文件名>`（可省略）：输出文件名，相对于 Emuera 可执行文件目录；省略或为空字符串时为 `emuera.log`。包含 `../` 或指向该目录之外时报错（弹出对话框，不输出）。
- `<是否省略环境信息>`（可省略）：为 1 时输出内容不含环境信息头部；省略时含头部。
- 返回值：恒为 1（不反映输出成功与否；失败时弹对话框）。
```erb
IF OUTPUTLOG("backup.log")
  PRINTL 日志已输出
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册（命令形式，已停用）：`Runtime/Script/Statements/FunctionIdentifier.cs:220-221`（`addFunction(FunctionCode.OUTPUTLOG, ...)` 两行均被注释）；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:79`（`//OUTPUTLOG,` 亦被注释）；switch 分支 `Runtime/Script/Process.ScriptProc.cs:600-610` 同样被注释
- 注册（函数形式）：`Runtime/Script/Statements/Function/Creator.cs:214`（`["OUTPUTLOG"] = new OutputlogMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7628`（`OutputlogMethod`，返回 `Int64`，参数类型 `(String, Int)`，全部可省略）；实际输出 `UI/Game/EmueraConsole.Print.cs:769`（`OutputLog(string filename, bool hideInfo)`）

```text
函数形式 OutputlogMethod.GetIntValue(exm, arguments):
    filename = (参数数 > 0) ? arguments[0] 求值 : ""
    hideInfo = (参数数 > 1 且 arguments[1] 求值 == 1)
    exm.Console.OutputLog(filename, hideInfo)
    return 1                                 # 恒返回 1

Console.OutputLog(filename, hideInfo):
    若 filename 为 "" 或 null:
        filename = ExeDir + "emuera.log"
    否则:
        filename = ExeDir + filename
    若 filename 含 "../":
        弹出对话框（不能输出到上级目录），return false
    若 filename 不以 ExeDir 开头（不区分大小写）:
        弹出对话框（只能输出到子目录），return false
    若 outputLog(filename, hideInfo) 成功:    # 写出 GetLog(hideInfo) 的内容，覆盖已有文件
        若窗口已创建:
            打印系统消息「日志文件已生成：<文件名>」并刷新
        return true
    否则:
        return false
```

系统侧自动输出：`Runtime/Script/Process.SystemProc.cs:183/200` 与 `UI/Game/EmueraConsole.cs:925-929` 在解析失败/分析模式/运行错误时调用 `OutputSystemLog(ExeDir + "emuera.log")`，并置 `noOutputLog = true` 防止重复输出。

## 备注

- 文档与源码的差异：文档把 `OUTPUTLOG` 记为无参数命令；本仓库已把该命令形式整体注释停用（`skipPrint` switch 中无此分支，脚本中写 `OUTPUTLOG` 会被当作未定义函数名处理），并提供了带文件名参数的式中函数形式。撰写 ERA 脚本时应按函数形式使用。
- 函数形式的第 2 参数（`hideInfo`）与文件名白名单限制（仅 ExeDir 子目录、禁 `../`）均不见于 ecd 文档，为 EE 扩展行为。
- zh 套件未收录本命令（仅 `Debug_Command.md` 提及调试命令 `@OUTPUT` 与 `OUTPUTLOG` 行为相同）。
