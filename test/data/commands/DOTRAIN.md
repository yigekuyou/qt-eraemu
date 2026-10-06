# DOTRAIN

- **类别**：命令（EE 扩展命令，流控制）
- **签名**：
  - `DOTRAIN <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「调试辅助·系统流的控制」小节（与 `CALLTRAIN` 相邻）；zh 套件仅在 `Difference.md` 提及「如果代码不打算在 Eramaker 中运行，考虑使用 `DOTRAIN` 或 `CALLTRAIN` 指令」，无独立小节。

## 语义

强制执行一次 TRAIN 命令。参数为 `train.csv` 中定义的命令编号；动作与玩家在命令选择中选中该命令完全相同：把参数赋值给 `SELECTCOM`，调用 `@EVENTCOM`，再根据 `@COM{SELECTCOM}` 的返回值走 `@SOURCE_CHECK`、`@EVENTCOMEND` 等系统流程。

使用限制（源码强制检查）：只能在 `@EVENTTRAIN`、`@SHOW_STATUS`、`@SHOW_USERCOM`、`@USERCOM`、`@EVENTCOMEND` 及从这些函数调用的函数内使用；在其它系统状态下执行会先在主控制台打印当前系统状态，然后抛出 CodeEE 错误「无法在此使用 DOTRAIN」。

参数检查：小于 0 或大于等于 `TRAINNAME` 的元素数时报错；除此之外不做检查——即使编号在 `train.csv` 中未定义也会强制尝试执行，也**不会调用 `@COM_ABLE*`**。需要检查时应像 ecd 示例那样在 `DOTRAIN` 之前自行判断。

另外，在 `CALLTRAIN` 处理途中执行 `DOTRAIN` 时，`CALLTRAIN` 的剩余部分会失效（源码执行 `coms.Clear(); isCTrain = false; count = 0`）。

## 用法

### `DOTRAIN <数值表达式>`
- `<数值表达式>`：`train.csv` 中定义的命令编号（不是游戏内显示值）。越界或为负时报错；未定义的编号会强制执行。
```erb
;在 @USERCOM 等上下文中强制执行 3 号命令
SIF ( X < 0 || X >= VARSIZE("TRAINNAME") || TRAINNAME:X == "" )
  RETURN
RESULT = 1
TRYCALLFORM COM_ABLE{X}
SIF RESULT == 0
  RETURN
DOTRAIN X
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:302`（`argb[FunctionArgType.INT_EXPRESSION]`，flag = `EXTENDED | FLOW_CONTROL`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:210`）
- 实现：switch-case 分发于 `Runtime/Script/Process.ScriptProc.cs:894`（`DOTRAIN` case）；系统状态机入口注册在 `Runtime/Script/Process.SystemProc.cs:35`（`Train_DoTrain → doTrain`），`doTrain()` 在 `Runtime/Script/Process.SystemProc.cs:474`

```text
ScriptProc 执行期 case FunctionCode.DOTRAIN:
    若 state.SystemState 不属于以下集合:
        Train_CallEventTrain / Train_CallShowStatus / Train_CallShowUserCom / Train_CallEventComEnd
        # 即 @EVENTTRAIN、@SHOW_STATUS、@SHOW_USERCOM、@USERCOM、@EVENTCOMEND 调用链
    则:
        主控制台打印 state.SystemState 字符串
        抛出 CodeEE（"无法在此处使用 DOTRAIN"）
    coms.Clear(); isCTrain = false; count = 0
        # 清空 CALLTRAIN 的命令队列 → 正在进行的 CALLTRAIN 剩余部分失效
    train = 参数表达式求值
    若 train < 0:                    抛出 CodeEE（"DOTRAIN 的参数小于 0"）
    若 train >= TrainName.Length:    抛出 CodeEE（"DOTRAIN 的参数超出 TRAINNAME 数组"）
    doTrainSelectCom = train
    state.SystemState = Train_DoTrain
    return false                     # 脚本暂停，交回系统状态机

系统状态机 doTrain()（Process.SystemProc.cs:474）:
    vEvaluator.UpdateAfterShowUsercom()
    vEvaluator.SELECTCOM = doTrainSelectCom     # 参数写入 SELECTCOM
    callEventCom():
        state.SystemState = Train_CallEventCom
        调用 @EVENTCOM（不存在时跳过）→ endEventCom():
            comName = "COM" + SELECTCOM
            state.SystemState = Train_CallComXX
            调用 @COM{SELECTCOM} → endCallComXX():
                若 RESULT == 0: 不经过 @EVENTCOMEND，直接进入 Train_CallEventComEnd 收尾（endCallEventComEnd）
                否则: 调用 @SOURCE_CHECK → 重置 SOURCE → 调用 @EVENTCOMEND
            # 之后回到 Train_CallEventTrain（SHOW_USERCOM/USERCOM 循环）
```

## 备注

- 文档说「初始化 UP、DOWN 等变量」发生在流程中（通过 `UpdateAfterShowUsercom`/`UpdateAfterInputCom` 一类的更新函数与正常命令执行共用同一条状态机路径），源码中 DOTRAIN 复用与手动选命令完全相同的 `Train_DoTrain → @EVENTCOM → @COMxx → @SOURCE_CHECK → @EVENTCOMEND` 流程，与文档描述一致。
- 文档列举的可用上下文与源码的 `SystemStateCode` 白名单一一对应（`@USERCOM` 调用期间对应 `Train_CallEventComEnd` 状态，源码注释也注明了这一点）。
- 错误时先 `PrintSystemLine(state.SystemState)` 再抛错——主控制台会多显示一行系统状态名，文档未提及。
- zh 套件无独立小节，仅 `Difference.md` 附带提及。
