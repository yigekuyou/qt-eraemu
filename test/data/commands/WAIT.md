# WAIT

- **类别**：命令
- **签名**：`WAIT`（无参数；与 Eramaker 不同，Emuera 中写 `WAIT 0` 这类带参数写法是错误的）
- **文档来源**：`ecd/docs/translation/Command.md` 未收录独立 `### WAIT` 小节（仅有 `FORCEWAIT`、`WAITANYKEY`、`TWAIT` 等相关小节及 `Difference.md` 的"`WAIT` 的行为"）；`Era-Chinese-Documentation/docs/`（zh/Command.md）未收录独立小节，但 `zh/ERB_File_Format.md`"输入与输入等待"与 `zh/Difference.md` 有语义描述。

## 语义

暂停脚本运行，等待玩家按下 Enter 键（或确认输入）后继续。常用于显示一段文字后停顿，让玩家看清内容；一般单行提示更适合用 `PRINTW`。在系统文本跳过（skip）状态下该指令会被跳过；若需要在跳过状态下也强制等待，用 `FORCEWAIT`。与 Eramaker 的行为差异：Eramaker 中执行 `WAIT` 不换行、按 Enter 时换行；Emuera 中若光标位于一行中间则执行 `WAIT` 时即换行，而按 Enter 或左键点击时不再换行。

## 用法

### WAIT
无参数，无返回值，副作用为暂停脚本等待 Enter 键。

```erb
PRINTL DATA输入开始。
WAIT
PRINTL 请输入你的年龄。
INPUT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:193`（`new WAIT_Instruction(false)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:750`（`WAIT_Instruction`，`isForce = false`）→ `UI/Game/EmueraConsole.cs:661`（`EmueraConsole.ReadAnyKey`）

```text
类 WAIT_Instruction(isForce):
    ArgBuilder <- VOID（无参数）
    flag <- IS_PRINT                       // 属于"显示相关"指令：skipPrint 期间整条被跳过
    函数 DoInstruction(exm, func, state):
        若 isForce:
            exm.Console.ReadAnyKey(anykey: false, stopMesskip: true)
        否则:
            exm.Console.ReadAnyKey(anykey: false, stopMesskip: false)

函数 ReadAnyKey(anykey = false, stopMesskip = false):    // EmueraConsole
    （若启用剪贴板监视则触发 AnyKeyWait 相关 CB 处理）
    req <- 新 InputRequest
    若 !anykey: req.InputType <- EnterKey    // WAIT 只接受 Enter/确认
    否则:       req.InputType <- AnyKey
    req.StopMesskip <- stopMesskip           // true 时（FORCEWAIT）不可被消息跳过打断
    inputReq <- req
    state <- ConsoleState.WaitInput          // 挂起脚本，等待输入事件
    process.NeedWaitToEventComEnd <- false
```

主循环（`Runtime/Script/Process.ScriptProc.cs:46`）在 `skipPrint && func.Function.IsPrint()` 时直接 `continue`，因此 WAIT 在系统文本跳过状态下不产生等待。`FORCEWAIT` 是同一个类 `WAIT_Instruction(true)`（`Runtime/Script/Statements/FunctionIdentifier.cs:202` 注册），仅 `stopMesskip` 不同，且执行时解除跳过状态。

## 备注

- ecd 与 zh 均无 `WAIT` 独立小节：语义取自 `ecd/Difference.md`（Eramaker 差异）、`ecd/Command.md` 的 `FORCEWAIT` 小节（"无法跳过的 WAIT"）、`zh/ERB_File_Format.md`（"通过显示句子等待输入时使用"）。
- zh 文档 `ERB_File_Format.md` 的示例中出现 `WAIT 0`，并标注为"错误"示例——与源码 `VOID` 参数（不接受参数）一致。
- 源码细节文档未提及：WAIT 与 WAITANYKEY 均带 `IS_PRINT` 标志，在 skipPrint（如 `SKIPDISP` 造成的快速跳过）期间会被整体跳过。
