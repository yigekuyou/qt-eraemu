# SAVEGAME

- **类别**：命令
- **签名**：`SAVEGAME`（无参数）
- **文档来源**：`ecd/docs/translation/ERB_File_Format.md`（存档相关）、`ERB_Commands.md`（存档表）、`Flow.md`（SaveGame 小节）；`Era-Chinese-Documentation/docs/Flow.md`（# SAVEGAME 小节）。ecd 主命令文档 `Command.md` 未设独立小节，仅在 SAVEDATA 小节提及「与 SAVEGAME 不同，SAVEDATA 可以在脚本的任何位置调用」。

## 语义

呼出标准存档界面（保存用）。它是 `BEGIN` 系流程指令：执行后把当前系统状态切换为「存档画面开始」（`SaveGame_Begin`），之后进入 `@SAVEINFO` 等系统流程；在实际写完存档之前调用 `@SAVEINFO`，存档注释由 `PUTFORM` 写入。

使用限制：只能在允许存档的系统状态（如 `SHOP` 流程）中使用。在不允许的状态下调用会抛出运行时错误并终止脚本。与 `SAVEDATA` 不同，`SAVEGAME` 不能在脚本任意位置调用，也不接受指定槽位参数。

执行时当前执行状态被备份、脚本让出控制权（交给标准存档界面）；存档写完或玩家取消后，系统流程用 `loadPrevState()` 把备份的状态放回，脚本**从本指令的下一行继续执行**——与 `BEGIN` 系不同，它不会永久终止当前函数。

## 用法

### SAVEGAME

无参数。

```erb
; 在 SHOP 流程中呼出存档界面
SAVEGAME
```

存档注释通过 `@SAVEINFO` 中的 `PUTFORM` 写入：

```erb
@SAVEINFO
PUTFORM {DAY}日目 {CALLNAME:MASTER}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:226`（`new SAVELOADGAME_Instruction(true)`，与 LOADGAME 共用同一实现类）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3074`（`SAVELOADGAME_Instruction`，`DoInstruction` 在 3083 行）；其调用的状态切换在 `Runtime/Script/Process.State.cs:242`（`SaveLoadData`）

```text
SAVELOADGAME_Instruction(isSave = true):
    构造: 无参数(VOID), 标记 FLOW_CONTROL

    DoInstruction(exm, func, state):
        if (state.SystemState & SystemStateCode.__CAN_SAVE__) != __CAN_SAVE__:
            funcName = state.Scope，为 null 时取 ""
            throw CodeEE(string.Format(trerror.CanNotUseInstruction.Text, funcName, "SAVEGAME/LOADGAME"))
            # 该文案（zh「不能使用{0}命令」）只有 1 个占位符，第 2 个实参被忽略 →
            # 实际消息只含当前函数名，不出现 "SAVEGAME/LOADGAME"

        GlobalStatic.Process.saveCurrentState(true)
        // 注：saveCurrentState 把当前 ProcessState 存进 prevStateList 后换成 Clone()，
        // Clone 不复制函数栈（因此新状态立刻处于 ScriptEnd），原 state 不可再引用

        GlobalStatic.Process.getCurrentState.SaveLoadData(true)
            → SaveLoadData(saveData):
                  if saveData: sysStateCode = SystemStateCode.SaveGame_Begin
                  else:        sysStateCode = SystemStateCode.LoadGame_Begin
                  return
        // 函数栈为空的当前状态使 runScriptProc 退出，系统状态机按 SaveGame_Begin
        // 进入存档界面（beginSaveGame:828 → 提问 → printSaveDataText），
        // 写档前调用 @SAVEINFO（存档注释用 PUTFORM 写入）

    界面收尾（Process.SystemProc.cs）:
        endCallSaveInfo(:1033): SaveTo(...) 写盘后 → loadPrevState()   # 恢复备份状态
        saveGameWaitInput(:967): 玩家取消(输入 100) → loadPrevState()   # 同样恢复
        → 脚本从 SAVEGAME 的下一行继续执行（不会永久丢弃原位置）
```

## 备注

- 与源码的差异：文档（`ERB_File_Format.md`）称两条命令「只能在 `SHOP` 中调用」；源码的判定条件是系统状态含 `__CAN_SAVE__` 标志，即任何允许存档的状态（含 SHOP 等）皆可，并非严格限定 SHOP。
- 报错文案：`SAVELOADGAME_Instruction` 给只有 1 个占位符的 `CanNotUseInstruction`（zh「不能使用{0}命令」）传了 2 个实参，`string.Format` 忽略多余实参 → 实际消息形如「不能使用{当前函数名}命令」，**不含**「SAVEGAME/LOADGAME」字样（源码疑似本意是使用两个占位符的 `CanNotUseInstructionInfunc`）。
- `SAVEGAME` 与 `LOADGAME` 共用 `SAVELOADGAME_Instruction`，仅以构造参数 `isSave` 区分。
- `zh/Flow.md` 补充了流程语义：调用 `@SAVEINFO` 的时机是在实际写完存档之前。源码层面「原位置被注销」只发生在存档界面期间（当前状态换成函数栈为空的新副本），界面结束后 `loadPrevState()` 会恢复，脚本继续往下走。
- ecd 的 `Command.md`（SAVEDATA 小节）明确二者区别：SAVEGAME 只能按系统流程呼出界面，SAVEDATA 可在脚本任意位置直接写指定槽位。
