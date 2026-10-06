# LOADGAME

- **类别**：命令
- **签名**：
  - `LOADGAME`
- **文档来源**：`ecd/docs/translation/ERB_Commands.md` 命令表（「`SAVEGAME` / `LOADGAME`：呼出标准存档 / 读取界面」）；`ecd/docs/translation/Flow.md`「LoadGame」流程；`ecd/docs/translation/ERB_Internal_Process.md`（`@TITLE_LOADGAME` 等）；zh 套件 `zh/Flow.md`「## # LOADGAME」可交叉核对。ecd `Command.md` 无 LOADGAME 独立小节（仅在 LOADDATA 小节中被对照提及）。

## 语义

呼出 Emuera 标准的读取存档界面（与 `SAVEGAME` 呼出的界面成对）。

- 只能在允许存读档的系统状态（`__CAN_SAVE__`，如 SHOP 流程）中使用；在其他函数/状态中执行会出错终止（错误信息里是当前函数名，详见备注）。
- 执行后脚本当前的执行状态被备份、控制权交给标准读档界面；玩家选择并读取某个存档后，流程转入读档后的系统流程（`@EVENTLOAD` 等），原执行位置被丢弃——此时 `LOADGAME` 不会「返回」。若玩家取消（输入 100），`loadPrevState()` 会恢复备份的状态，脚本从 `LOADGAME` 的下一行继续。
- 若定义了 `@TITLE_LOADGAME`，标准标题画面选择「读取存档」时会调用它（与 `@LOADGAME` 的画面略有不同，见 `zh/Flow.md`）。

## 用法

### `LOADGAME`
- 无参数、无返回值；仅限可存读档状态（SHOP 等）使用。
```erb
;SHOP 流程内提供读档入口
PRINTL [1] 读取存档
INPUT
IF RESULT == 1
  LOADGAME
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:227`（`new SAVELOADGAME_Instruction(false)`——与 SAVEGAME(true) 共用一类，`isSave=false`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3074`（`SAVELOADGAME_Instruction`，flag = `FLOW_CONTROL`，参数 builder `VOID`）；状态转移在 `Runtime/Script/Process.State.cs:242`（`SaveLoadData`）；界面绘制在 `Runtime/Script/Process.SystemProc.cs`（`beginLoadGame`）

```text
DoInstruction(exm, func, state):
    若 (state.SystemState & SystemStateCode.__CAN_SAVE__) != __CAN_SAVE__:
        funcName = state.Scope（为 null 则取 ""）
        抛出 CodeEE（string.Format(trerror.CanNotUseInstruction.Text, funcName, "SAVEGAME/LOADGAME")）
        # 该文案（zh「不能使用{0}命令」）只有 1 个占位符，第 2 个实参被忽略 →
        # 实际消息只有当前函数名，不出现 "SAVEGAME/LOADGAME"
    GlobalStatic.Process.saveCurrentState(true)
    # 当前执行状态（脚本位置、调用栈等）备份进 prevStateList，当前状态换成 Clone()；
    # Clone 不复制函数栈，新状态因此立刻 ScriptEnd，原 state 不可再用
    GlobalStatic.Process.getCurrentState.SaveLoadData(isSave = false)

SaveLoadData(saveData = false):
    sysStateCode = SystemStateCode.LoadGame_Begin   # SAVEGAME 时为 SaveGame_Begin

系统状态机（LoadGame_Begin → beginLoadGame，`Runtime/Script/Process.SystemProc.cs:835`）:
    console.PrintSingleLine(读取提问文本)
    state.SystemState = LoadGame_Begin
    printSaveDataText()             # 显示存档列表，进入标准读档界面
    # 玩家选定存档读取后：deletePrevState() 丢弃原位置，转入 @EVENTLOAD 等读档后流程
    # 玩家取消(输入 100，loadGameWaitInput:1046)：loadPrevState() 恢复备份状态，脚本从 LOADGAME 下一行继续
```

## 备注

- 文档与源码一致：`LOADGAME` 是流控制指令（`FLOW_CONTROL`）、无参数；「只能在可存读档状态使用」对应 `__CAN_SAVE__` 位检查，且错误信息里带当前函数名——这是 ecd `Command.md` 未描述的细节。
- 报错文案细节：源码给只有 1 个占位符的 `CanNotUseInstruction`（zh「不能使用{0}命令」）传了 2 个实参，`string.Format` 忽略多余的 `"SAVEGAME/LOADGAME"`，因此实际消息形如「不能使用{当前函数名}命令」，不含「SAVEGAME/LOADGAME」字样（与 `SAVEGAME.md` 备注一致）。
- zh `Flow.md`「当执行 `LOADGAME` 指令时……`LOAD` 被执行时就会忘记原来的位置并过渡到 `LOADDATAEND`」描述的是标准读档画面的流程语义；与 `LOADDATA`（脚本任意处可用、读后继续原流程）的差异在 `ecd/Command.md` LOADDATA 小节有对照说明。
- 实现细节（源码注释）：`saveCurrentState(true)` 之后旧 `state` 对象进入备份，代码必须改用 `getCurrentState`——这是源码层面的实现要点，不影响 ERB 语义。
- 与 `SAVEGAME` 共用 `SAVELOADGAME_Instruction`，仅 `isSave` 布尔不同。
