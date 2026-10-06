# CALLTRAIN

- **类别**：命令
- **签名**：
  - `CALLTRAIN <命令数>`
- **文档来源**：`ecd/docs/translation/Command.md`「调试辅助·系统流的控制」→「CALLTRAIN `<命令数>`」；`Era-Chinese-Documentation/docs/Function_and_Preprocessor.md`「# @CALLTRAINEND」、`docs/Flow.md`（提及）、`docs/Difference.md`（与 NEXTCOM/DOTRAIN 的关系）。

## 语义

连续自动执行多个 TRAIN 命令。事先把要执行的命令编号赋值给 `SELECTCOM:(1～)`（`SELECTCOM:1` 起依次排列），然后以要执行的命令数量为参数执行 `CALLTRAIN`。引擎会按 `SELECTCOM:1, SELECTCOM:2, …, SELECTCOM:<命令数>` 的顺序，对每个命令执行与玩家手动选择时完全相同的流程：初始化 `UP`/`DOWN` 等变量 → 赋值 `SELECTCOM` → 调用 `@EVENTCOM` → `@COM{SELECTCOM}` → `@SOURCE_CHECK` → `@EVENTCOMEND` → 循环。

与普通命令执行一样会调用 `@SHOW_STATUS` 和 `@SHOW_USERCOM`，但不显示 TRAIN 命令菜单与 USERCOM 询问（自动跳过打印）；若想显示 USERCOM，可用 `NOSKIP`～`ENDNOSKIP`。注意：命令编号使用 `TRAIN.CSV` 中指定的编号，而非画面显示的选项编号。

全部命令执行完后调用系统函数 `@CALLTRAINEND`（非事件函数，不能多重定义）。另外：在 `CALLTRAIN` 处理途中执行 `DOTRAIN` 会使剩余队列失效；`BEGIN` 系切换或 `STOPCALLTRAIN` 会中途清空队列。

## 用法

### `CALLTRAIN <命令数>`
- `<命令数>`：数值表达式，要连续执行的命令个数 N；引擎读取 `SELECTCOM:1`～`SELECTCOM:N`。N 大于等于 `SELECTCOM` 数组长度时抛 CodeEE。
```erb
SELECTCOM:1 = 0    ;TRAIN.CSV 中编号为 0 的命令
SELECTCOM:2 = 3
SELECTCOM:3 = 7
CALLTRAIN 3
;连续执行 3 个命令，结束后调用 @CALLTRAINEND

@CALLTRAINEND
PRINTL 连续命令执行完毕。
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:300`（`argb[FunctionArgType.INT_EXPRESSION], EXTENDED | FLOW_CONTROL`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:878`（`case FunctionCode.CALLTRAIN:`）；队列构建在 `Runtime/Script/Process.cs:264`（`SetCommnds`）；训练循环在 `Runtime/Script/Process.SystemProc.cs:306` 起（`isCTrain`/`coms`/`count` 及 `endCallShowUserCom`、`trainWaitInput`、`endCallEventComEnd` 等）

```text
case FunctionCode.CALLTRAIN:            # Process.ScriptProc.cs:878
    count = 参数表达式.GetIntValue(exm)
    SetCommnds(count)
    return false                        # 交还系统流程处理

SetCommnds(count):                      # Process.cs:264
    coms = 新列表; isCTrain = true
    selectcom = vEvaluator.SELECTCOM_ARRAY
    若 count >= selectcom.Length:
        抛 CodeEE（CALLTRAIN 参数超过 SELECTCOM 数组大小）
    for i in 0 .. count-1:
        coms.Add(selectcom[i+1])        # 取 SELECTCOM:1 .. SELECTCOM:N

# 之后进入正常的 TRAIN 状态机，isCTrain=true 时自动推进：
endCallShowUserCom():                   # Process.SystemProc.cs:412 附近
    若 isCTrain:
        若 count < coms.Count:
            systemResult = coms[count]; count++
            trainWaitInput()            # 把队列中的命令当作"玩家选择"执行
    # 非 CALLTRAIN 时则进入数值输入等待

trainWaitInput():
    若 isCTrain:
        若该命令在 comAble 中可用 → SELECTCOM = 命令编号; callEventCom()
        否则 → 打印"无法执行该命令"，RESULT = 命令编号，调用 @USERCOM
    callEventCom → @EVENTCOM → @COM{SELECTCOM} → RESULT!=0 时
    @SOURCE_CHECK → @EVENTCOMEND

endCallEventComEnd():
    若 isCTrain 且 count == coms.Count: # 队列执行完毕
        isCTrain = false; coms.Clear(); count = 0
        callFunction("CALLTRAINEND")    # 收尾函数，非事件函数
    否则: endCallEventTrain()           # 回到 SHOW_STATUS 继续

SKIP 控制：isCTrain 时 skipPrint = true（endCallEventTrain），
    命令菜单（PrintC）与 USERCOM 选择提示不再打印；
    @CALLTRAINEND 调用前 skipPrint 恢复 false。

case FunctionCode.STOPCALLTRAIN:        # 同文件 885 行
    若 isCTrain: ClearCommands(); skipPrint = false

ClearCommands():                        # Process.cs:279
    coms.Clear(); count = 0; isCTrain = false; skipPrint = true
    return callFunction("CALLTRAINEND", false, false)
```

## 备注

- 文档「参数对应的编号是 TRAIN.CSV 中指定的值，而不是游戏中的值」与实现一致：`coms` 直接存放 `SELECTCOM:(1..N)` 的数值，经 `comAble` 检查后赋给 `SELECTCOM` 再走 `@COM{编号}`。
- 文档说「不显示 TRAIN 命令和 USERCOM」对应 `skipPrint=true` 及 `endCallComAbleXX`/`endCallShowUserCom` 中 `if (!isCTrain)` 才打印菜单的分支；`NOSKIP`～`ENDNOSKIP` 可控制该跳过行为。
- `zh/Difference.md` 指出与 Eramaker 兼容的 `NEXTCOM` 会反复执行 COM0，若代码不打算在 Eramaker 运行，建议改用 `DOTRAIN` 或 `CALLTRAIN`；`zh/Function_and_Preprocessor.md` 记载 `@CALLTRAINEND` 在自动执行结束后由系统内部自动调用，且不能多重定义——与 `ClearCommands`/`endCallEventComEnd` 的 `callFunction("CALLTRAINEND")` 一致。
- ecd 文档「在 CALLTRAIN 的处理途中执行 DOTRAIN 时，CALLTRAIN 的剩余部分会失效」对应 `DOTRAIN` 分支中 `coms.Clear(); isCTrain = false; count = 0`（Process.ScriptProc.cs:913）。
