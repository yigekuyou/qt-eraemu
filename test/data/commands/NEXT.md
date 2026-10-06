# NEXT

- **类别**：命令
- **签名**：
  - `NEXT`
- **文档来源**：`ecd/docs/translation/Command.md`「FOR～NEXT」相关小节（`### NEXT`）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

结束由 `FOR` 开始的循环。`FOR <变量名>, <起始值>, <终止值>{, <步长>}` 每次执行到 `NEXT` 时把 `<步长>` 加到循环变量上，若循环变量尚未越过 `<终止值>` 则跳回 `FOR` 继续下一轮，否则结束循环从 `NEXT` 下一行继续。`NEXT` 与 `REPEAT` 的 `REND` 共用同一实现，区别仅在 `FOR` 侧的参数形式（起始值/终止值/步长）。

用 `GOTO` 等指令直接跳入 `FOR`～`NEXT` 内部时（未经 `FOR` 初始化循环变量），与 `REPEAT`～`REND` 一样：会执行到 `NEXT`，然后忽略 `NEXT` 的回跳并从下一行继续处理。`FOR`～`NEXT` 可以嵌套（装载期只在内层计数变量与外层同名，或内层用与 `REPEAT` 相同的 `COUNT:0` 时告警）；`REPEAT`～`REND` 则不可嵌套，装载期会对嵌套的 `REPEAT` 告警。

## 用法

### `NEXT`
- 无参数。对应最近的 `FOR` 行；循环变量、终止值、步长均由 `FOR` 决定。
```erb
;X 从 0 到 9 输出
FOR X, 0, 9
  PRINTFORML X = {X}
NEXT
;步长为负：从 10 递减到 1
FOR X, 10, 1, -1
  PRINTFORML X = {X}
NEXT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:249`（`new REND_Instruction(), EXTENDED`）；与 `REND`（同文件 247 行）共用同一个类；解析期父子配对 `funcParent[FunctionCode.NEXT] = FunctionCode.FOR`（同文件 490 行）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3521`（`REND_Instruction`，无参数，标志 `METHOD_SAFE | FLOW_CONTROL | PARTIAL`）

```text
case NEXT (REND_Instruction.DoInstruction):
    jumpTo = (InstructionLine)func.JumpTo        # 解析期指向对应的 FOR 行
    若 jumpTo.LoopCounter == null:
        # 循环变量不明（未经 FOR/REPEAT 就进入循环，如 GOTO 跳入）
        state.JumpTo(jumpTo.JumpTo)              # 跳到循环结束之后的行，忽略本次 NEXT
        return
    unchecked:
        jumpTo.LoopCounter.ChangeValue(jumpTo.LoopStep, exm)   # 循环变量 += 步长
    counter = jumpTo.LoopCounter 求值
    若 (步长 > 0 且 counter < LoopEnd) 或 (步长 < 0 且 counter > LoopEnd):
        state.JumpTo(func.JumpTo)                # 还有剩余次数，跳回 FOR 行
    # 否则结束循环，落到 NEXT 的下一行
```

`FOR` 侧（`REPEAT_Instruction(true)`，`Runtime/Script/Statements/Instraction.Child.cs:3098`）首次执行时：把循环变量设为 `<起始值>`，记录 `LoopEnd`/`LoopStep`；若步长为正且起始值已越过终止值（或步长为负且起始值已低于终止值），直接跳到 `NEXT` 之后，循环体一次也不执行。

## 备注

- `NEXT` 与 `REND` 是同一个类 `REND_Instruction` 的两个注册名，语义唯一差别是入口 `FOR` / `REPEAT` 的参数形式与 `EXTENDED` 标志。
- 文档说明「除步长外，各值在循环内固定」：`LoopEnd`/`LoopStep` 在 `FOR` 首次执行时求值并缓存，循环体内改变相关变量不影响判断；循环变量本身则每轮被 `ChangeValue` 递增。
- 步长为 0 时：`REND_Instruction` 的两个分支条件都不成立（步长既不大于 0 也不小于 0），首次 `FOR` 时循环体也不执行（`REPEAT_Instruction` 中两个「还有剩余」分支均不成立而直接跳到循环后）。
- zh 套件未收录本命令。
