# LOADDATA

- **类别**：命令
- **签名**：
  - `LOADDATA <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「游戏存档的操作」LOADDATA 小节；zh 套件 `Command.md` 未收录（`zh/Flow.md` 流程文档中描述了 `LOADDATA`/`LOADDATAEND` 的系统流程）。

## 语义

读取第 1 参数所示编号的存档文件中的数据。

- 读取失败时会**出错终止**，因此请务必先用 `CHKDATA` 检查能否读取（`RESULT:0 == 0` 才可读）。
- 与 `LOADGAME` 不同，`LOADDATA` 可以在脚本的任何位置调用（不受「只能在 SHOP 等状态使用」限制）。
- 读取成功后系统会依次调用 `@SYSTEM_LOADEND`（不存在则跳过）与 `@EVENTLOAD`（不存在则跳过），然后流程继续。
- 参数为负数或超过 int 上限时出错。

## 用法

### `LOADDATA <数值表达式>`
- `<数值表达式>`：存档编号。
```erb
CHKDATA 5
IF RESULT:0 == 0
  LOADDATA 5
ELSE
  PRINTL 5 号存档无法读取
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:229`（`argb[FunctionArgType.INT_EXPRESSION], EXTENDED | FLOW_CONTROL`——走 switch-case 分发，无独立指令类）
- 实现：`Runtime/Script/Process.ScriptProc.cs:810`（`doFlowControlFunction` 的 `case FunctionCode.LOADDATA` 分支）；数据读取在 `VariableEvaluator.CheckData / LoadFrom`；状态机收尾在 `Runtime/Script/Process.SystemProc.cs:801`（`beginDataLoaded`）

```text
case LOADDATA（doFlowControlFunction 内）:
    target = ((ExpressionArgument)func.Argument).Term.GetIntValue(exm)
    若 target < 0:
        抛出 CodeEE（“LOADDATA 的参数不能为负”）
    否则若 target > int.MaxValue:
        抛出 CodeEE（“LOADDATA 的参数过大”）
    result = vEvaluator.CheckData((int)target, EraSaveFileType.Normal)
    若 result.State != OK:
        抛出 CodeEE（“读取的数据已损坏”）     # 即文档说的“读取失败时出错终止”
    若 !vEvaluator.LoadFrom((int)target):     # 实际读入变量数据
        抛出 ExeEE（“LOADDATA 发生意外错误”）
    state.ClearFunctionList()                 # 清空调用函数栈
    state.SystemState = SystemStateCode.LoadData_DataLoaded
    返回 false                                # 本行之后的脚本暂止，交由系统状态机接管

系统状态机（LoadData_DataLoaded → beginDataLoaded）:
    state.SystemState = LoadData_CallSystemLoad
    callFunction("SYSTEM_LOADEND")            # 不存在则跳过
    → endSystemLoad: 卸载临时图像资源
    → callFunction("EVENTLOAD")               # 不存在则跳过
    → 流程继续（此后回到脚本执行）
```

## 备注

- 文档「与 `LOADGAME` 不同，可以在脚本的任何位置调用」对应源码：`LOADGAME`（`SAVELOADGAME_Instruction`）执行前检查 `SystemStateCode.__CAN_SAVE__`，`LOADDATA` 的 switch-case 无此检查。
- zh `Flow.md` 描述「`LOADDATA` 指令与 `CALL` 指令一样返回原处」（即读档后流程回到调用处继续），与实现吻合：函数栈被清除，但系统状态机走完 `@SYSTEM_LOADEND`/`@EVENTLOAD` 后脚本从逻辑上继续（该文档同时指出读档执行时位置被丢弃、转入 `@LOADDATAEND` 流程的说法针对的是标准读档画面流程，两处语境不同，如实两记）。
- 文档建议先用 `CHKDATA` 检查；源码中 `CheckData` 状态非 OK 时确实直接抛错终止，与文档一致。
- `LOADDATA` 的第 1 参数在索引中被标为 `EXTENDED | FLOW_CONTROL`：它是流控制指令（会改变系统状态），这一点文档未明说，但「与 LOADGAME 不同……任何位置调用」暗示了它仍属存档/流程系指令。
