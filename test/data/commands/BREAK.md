# BREAK

- **类别**：命令
- **签名**：
  - `BREAK`
- **文档来源**：`ecd/docs/translation/Command.md` 无专节；`ecd ERB_Compound_Statements.md`「循环控制」表（`BREAK`：立即跳出当前循环）与 `ERB_File_Format.md`「重复与GOTO」（REPEAT～REND 中的行为）；`Era-Chinese-Documentation/docs/ERB_File_Format.md` 同段（含示例）。

## 语义

立即跳出当前所在的循环（`REPEAT`～`REND`、`FOR`～`NEXT`、`WHILE`～`WEND`、`DO`～`LOOP`），跳转到循环结束语句的下一行继续执行。对 `REPEAT`/`FOR` 这类带计数器的循环，跳出时会像正常走完一轮那样把步长累加进 `COUNT`（Eramaker 时代遗留行为）；对 `WHILE`/`DO` 这类无计数器循环则直接跳出。不能用于 `SELECTCASE`（不能用 `BREAK` 跳入 `ENDSELECT`）。无参数。

## 用法

### `BREAK`
- 无参数。只能写在循环体内部（编译期会检查所在的循环）。
```erb
MONEY = 300
REPEAT 5
  SIF MONEY <= COUNT * 100
    BREAK
  PRINTFORML 金额比{COUNT*100}元更多。
REND
;MONEY=300 时只打印到「金额比200元更多」为止
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:255`（`new BREAK_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3440`（`BREAK_Instruction`）

```text
指令类 BREAK_Instruction:
    flag = METHOD_SAFE | FLOW_CONTROL
    参数为 VOID

    DoInstruction(exm, func, state):
        jumpTo = func.JumpTo          # 所在循环的起点行（REPEAT/FOR/WHILE/DO）
        iLine  = jumpTo.JumpTo        # 该循环的结束语句（REND/NEXT/WEND/LOOP）
        若 jumpTo.FunctionCode != WHILE 且 != DO:
            # 带计数器的循环：BREAK 时也推进计数器（eramaker では BREAK 時に COUNT が回る）
            jumpTo.LoopCounter.ChangeValue(jumpTo.LoopStep, exm)
        state.JumpTo(iLine)           # 跳到循环结束语句（其语义为落到下一行）
```

## 备注

- 源码注释说明了 1.723 的规格变更：BREAK 记住的跳转目标是 REPEAT/FOR/WHILE 这一行本身（`func.JumpTo`），真正的落点是它再指向的 REND/NEXT 等（`jumpTo.JumpTo`），因此 `WHILE`/`DO` 无计数器时「即ジャンプ」。
- 「BREAK 时 COUNT 也会 +步长」并非只可从源码读出：副本文档 `zh/Difference.md`「COUNT是在REPEAT-REND的结尾处添加的」小节已记载该兼容性行为（「如果你用 `BREAK` 退出，计数也是 +1」「在 `FOR`-`NEXT` 语法中，循环变量也是 +1」），源码注释（`eramakerではBREAK時にCOUNTが回る`）与之对应：BREAK 后 `COUNT` 的值等于「当前轮次 + 1 × 步长」。
- ecd 文档特别指出 `SELECTCASE` 中不能使用 `BREAK`（与 `switch` 的 fall-through 对比），这是分支语法而非循环的限制。
