# STOPCALLTRAIN

- **类别**：命令
- **签名**：STOPCALLTRAIN（无参数）
- **文档来源**：`ecd/docs/translation/Command.md`（`### STOPCALLTRAIN` 小节）；Era-Chinese-Documentation 未收录本命令

## 语义

强制结束 `CALLTRAIN` 指令流程。在 `CALLTRAIN` 处理期间被调用时，会立刻终止剩余的 `CALLTRAIN` 处理；其他情况下什么都不做。

本命令被标记为流控制（FLOW_CONTROL）类命令。

## 用法

### STOPCALLTRAIN

无参数。通常在 `CALLTRAIN` 期间会被调用的函数（如 `@USERCOM`、事件函数等）中调用，用于中途打断调教循环。

```erb
@USERCOM
IF RESULT == 999
    STOPCALLTRAIN
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:301`（`argb[FunctionArgType.VOID]`，EXTENDED | FLOW_CONTROL）
- 实现：`Runtime/Script/Process.ScriptProc.cs:885`（`case FunctionCode.STOPCALLTRAIN`）；`ClearCommands` 在 `Runtime/Script/Process.cs:279`

```text
case STOPCALLTRAIN:
    若 isCTrain（当前正处于 CALLTRAIN 驱动的指令队列处理中）:
        ClearCommands()
        skipPrint = false          // ClearCommands 内部置 true，这里又改回 false
    返回 false（结束本次指令流程处理，交回系统状态机）

Process.ClearCommands():
    coms.Clear()                   // 清空待执行的命令编号队列
    count = 0
    isCTrain = false
    skipPrint = true
    return callFunction("CALLTRAINEND", false, false)   // 调用 @CALLTRAINEND
```

对照：`CALLTRAIN`（`Runtime/Script/Process.ScriptProc.cs:878`）执行时调用 `SetCommnds(count)`（`Runtime/Script/Process.cs:264`）：从 `SELECTCOM_ARRAY` 取前 count 个命令装入 `coms` 队列并置 `isCTrain = true`，随后 `return false` 切入调教系统流程。

## 备注

- **文档未提及的源码行为**：`STOPCALLTRAIN` 在 `CALLTRAIN` 有效时经由 `ClearCommands()` 会调用 `@CALLTRAINEND` 函数（与 `CALLTRAIN` 自然结束时的收尾路径相同）；同时会把 `skipPrint` 先置 true 再立即置回 false，净效果是关闭显示忽略。文档只说「结束 CALLTRAIN 的处理」，实现细节已如上补记。
- 若不在 `CALLTRAIN` 期间调用（`isCTrain == false`），本命令除了结束当前指令行外不产生任何副作用（不会调用 `@CALLTRAINEND`）。
- Era-Chinese-Documentation 套件未收录本命令。
