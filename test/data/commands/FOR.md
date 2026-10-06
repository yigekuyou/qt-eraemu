# FOR

- **类别**：命令（EE 扩展语法，FOR～NEXT）
- **签名**：`FOR <数值型变量>, <数值表达式>(初始值), <数值表达式>(终止值){, <数值表达式>(步长)}`
- **文档来源**：`ecd/docs/translation/Command.md`「循环·分支语法」`### FOR` 小节；`Era-Chinese-Documentation`（zh 套件）未收录该命令

## 语义

`FOR`～`NEXT` 是 `REPEAT`～`REND` 的增强版：可自行指定计数变量（REPEAT 固定用 `COUNT:0`）、初始值（REPEAT 固定 0）和步长（REPEAT 固定 1）。执行到 `FOR` 时把初始值代入变量，循环体执行到对应 `NEXT` 时把步长加到变量上，只要还没"超过"终止值就跳回 `FOR` 之后继续；与 `REPEAT` 一样可用 `CONTINUE`/`BREAK`，且可嵌套（嵌套时各层变量名必须不同）。步长为负时向终止值减小的方向循环（Emuera 扩展；原版 eramaker 系文档通常只描述正步长）。

## 用法

### FOR <数值型变量>, <数值表达式>, <数值表达式>{, <数值表达式>}
- 第 1 参 `<数值型变量>`：计数用变量名，必须是可赋值的数值变量，且不能是角色变量；不得与本循环外层正在使用的其他循环变量同名。
- 第 2 参：初始值。
- 第 3 参：终止值（判断基准，见下方实现语义）。
- 第 4 参：步长，可省略，默认 1。

```erb
FOR COUNT, 0, X
  ～～
NEXT
;与 REPEAT X ... REND 几乎等价

FOR Y, 0, 100
  FOR X, 0, 100
    ～～
  NEXT
NEXT
;嵌套示例

FOR I, 10, 0, -1
  PRINTFORL {I}
NEXT
;负步长：10,9,...,0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:248`（`new REPEAT_Instruction(true)`，flag = METHOD_SAFE | FLOW_CONTROL | PARTIAL | EXTENDED；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:192`；`NEXT` 注册为 `new REND_Instruction()`，:249）
- 参数构建：`Runtime/Script/Statements/ArgumentBuilder.cs:1653`（`SP_FOR_NEXT_ArgumentBuilder`，minArg=3；第 1 参必须是可赋值变量且不能是角色数据，步长缺省为常量 1）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3098`（`REPEAT_Instruction`，与 REPEAT 共用；`NEXT` 走 `Runtime/Script/Statements/Instraction.Child.cs:3521` 的 `REND_Instruction`）

```text
# FOR 行（REPEAT_Instruction.DoInstruction）
forArg ← (SpForNextArgment)func.Argument
func.LoopCounter ← forArg.Cnt（计数变量）
func.LoopCounter ← 求值 forArg.Start（把初始值写入计数变量）
func.LoopEnd   ← 求值 forArg.End
func.LoopStep  ← 求值 forArg.Step
若 (LoopStep > 0 且 LoopEnd > LoopCounter 当前值)
    或 (LoopStep < 0 且 LoopEnd < LoopCounter 当前值):
    return            # 循环条件仍成立，顺序执行循环体
否则:
    state.JumpTo(func.JumpTo)   # 跳到对应 NEXT 之后（一次性都不执行）

# NEXT 行（REND_Instruction.DoInstruction）
jumpTo ← func.JumpTo（对应的 FOR 行）
若 jumpTo.LoopCounter 为 null:      # 未经过 FOR/REPEAT 就进来的循环
    state.JumpTo(jumpTo.JumpTo)     # 忽略并跳出循环（沿用 eramaker 仕样）
否则:
    unchecked: jumpTo.LoopCounter += LoopStep   # 步长加进计数变量（溢出按 unchecked 处理）
    counter ← LoopCounter 当前值
    若 (LoopStep > 0 且 LoopEnd > counter) 或 (LoopStep < 0 且 LoopEnd < counter):
        state.JumpTo(func.JumpTo)   # 未达终止值，跳回 FOR 行再执行
    # 否则顺序执行 NEXT 的下一行，循环结束
```

注意源码的判定是严格比较（`>` / `<`）：正步长时循环继续的条件是"计数值仍小于终止值"，因此 `FOR I,0,3` 执行 I=0,1,2 共 3 次；计数等于终止值时即停止（与 ecd 文档"直到超过第 3 参数"的措辞略有出入，实际是"到达即停"）。

## 备注

- 文档与源码的差异：ecd 文档说"每次把步长加到变量上，直到超过终止值"，源码实际在计数值等于终止值时就结束循环（严格小于/大于比较），即终止值本身不会成为循环体中观察到的计数值（正步长时）。
- 加载期检查（`Runtime/Script/Loader/ErbLoader.cs:1019-1080`，nestCheck 的 `case REPEAT` / `case FOR`）：嵌套 `REPEAT` 会告警；嵌套 `FOR` 若内层计数变量与外层同名、或写成 `COUNT:0`（与 REPEAT 的计数器冲突）也会告警。
- 与 REPEAT 共用同一个指令类 `REPEAT_Instruction`，仅参数构建器不同。
