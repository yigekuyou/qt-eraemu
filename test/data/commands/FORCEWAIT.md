# FORCEWAIT

- **类别**：命令
- **签名**：`FORCEWAIT`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md`「输入处理」`### FORCEWAIT` 小节；`Era-Chinese-Documentation`（zh 套件）未收录该命令

## 语义

与 `WAIT` 一样等待玩家按回车，但这个等待无法被右键点击或宏（连续读取跳过）跳过；并且在执行到该指令时，正在进行的跳过状态会被强制解除（StopMesskip）。无参数、无返回值。用于重要提示处强制玩家确认。

## 用法

### FORCEWAIT
无参数。

```erb
PRINTL 重要的消息……
FORCEWAIT
PRINTL 玩家必须按回车才能继续
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:202`（`new WAIT_Instruction(true)`，flag = IS_PRINT；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:49`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:750`（`WAIT_Instruction`，与 `WAIT` 共用类，由构造参数 `force` 区分）

```text
构造：force = true（FORCEWAIT 时）；flag = IS_PRINT

DoInstruction(exm, func, state):
    若 force:
        exm.Console.ReadAnyKey(anykey: false, stopMesskip: true)
        # 等待回车（EnterKey），且请求带有 StopMesskip 标记
    否则:                       # 普通 WAIT
        exm.Console.ReadAnyKey()   # 等待回车，不带 StopMesskip

# EmueraConsole.ReadAnyKey（UI/Game/EmueraConsole.cs:661）：
#   生成 InputRequest{ InputType = EnterKey, StopMesskip = stopMesskip }
#   进入 WaitInput 状态等待输入
```

## 备注

- ecd 文档描述"无法通过右键或宏跳过""执行到该指令时跳过状态被解除"，与源码 `StopMesskip = true` 的语义一致。
- 与 `WAIT` 的唯一实现差异就是构造参数 `force=true`；与 `WAITANYKEY`（任意键、不停止跳过）不同，FORCEWAIT 仍只认回车键。
- 其余无冲突。
