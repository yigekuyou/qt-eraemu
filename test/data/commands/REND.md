# REND

- **类别**：命令
- **签名**：`REND`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md` 未收录独立小节（`REPEAT`～`REND` 的说明见其「循环·分支语法」FOR 节及跳入循环的行为说明）；`Era-Chinese-Documentation/docs/ERB_File_Format.md`（「重复与GOTO」节：「语句会执行到 `REND` 处后返回到初始点，重复次数储存在 `COUNT` 变量中」）。

## 语义

结束由 `REPEAT` 开始的计数循环（`REPEAT`～`REND` 必须成对，`REND` 不能单独使用）。

- 执行到 `REND` 时，循环计数器 `COUNT:0` 加 1（按 `REPEAT` 设定的步长，REPEAT 固定为 1）；若 `COUNT` 仍小于 `REPEAT` 指定的次数，则跳回 `REPEAT` 的下一行继续循环，否则继续执行 `REND` 的下一行。
- 循环体内可用 `CONTINUE` 跳回 `REPEAT` 处、`BREAK` 跳出循环。
- 用 `GOTO` 等直接跳入 `REPEAT`～`REND` 内部时，会执行到 `REND` 之前，然后忽略 `REND` 并从下一行继续处理（此时循环变量未知，REND 直接退出循环）——这是 eramaker 的历史行为，Emuera 重现之（ecd/Command.md「循环·分支语法」节）。
- 退出循环（含 BREAK）时 `COUNT` 已被 +1，与一般语言的 for/break 不同（ecd/Difference.md「`REPEAT`-`REND` 结束时 `COUNT` 会增加」）。
- `REPEAT` 不支持嵌套（嵌套时加载期警告）。

## 用法

### `REND`
无参数；与最近的 `REPEAT` 配对。

```erb
REPEAT 10
    PRINTL 你好
REND
PRINTL            ; 不带参数的 PRINTL 用于换行
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:247`（`new REND_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3521-3547`（类 `REND_Instruction`，flag = `METHOD_SAFE | FLOW_CONTROL | PARTIAL`）；`REPEAT`↔`REND` 的配对与跳转标签装配在 `Runtime/Script/Loader/ErbLoader.cs:1225-1241`（`REND.JumpTo = REPEAT行`，`REPEAT.JumpTo = REND行`）

```text
执行期（REND_Instruction.DoInstruction）：
    jumpTo = (InstructionLine)func.JumpTo        ; 即配对的 REPEAT 行
    ; 循环变量不明（没经 REPEAT/FOR 就跳进循环体）时，无视并退出循环
    ; （eramaker 的历史规格）
    若 jumpTo.LoopCounter == null：
        state.JumpTo(jumpTo.JumpTo)              ; 跳到 REPEAT 行记录的出口（REND 之后），循环结束
        return
    jumpTo.LoopCounter.ChangeValue(jumpTo.LoopStep, exm)   ; COUNT += 步长（unchecked）
    counter = jumpTo.LoopCounter.GetIntValue(exm)
    若 ((步长 > 0 且 终值 > counter) 或 (步长 < 0 且 终值 < counter))：
        state.JumpTo(func.JumpTo)                ; 跳回 REPEAT 行 → 下一行（循环体开头）继续
    ; 否则落空，继续执行 REND 的下一行

加载期（ErbLoader）：
    遇到 REPEAT 时若嵌套栈中已有 REPEAT → 警告「REPEAT 不能嵌套」；
    REND 与栈顶 REPEAT 配对（不匹配则报「缺少对应的 REPEAT」并标为错误行）。
```

## 备注

- `zh/ERB_File_Format.md:913` 说「`REPEAT` 语句不支持嵌套」；Emuera 源码对嵌套只是加载期**警告**（非错误），外层 REPEAT 在内层 REND 退出后仍会继续循环——文档与源码在此有差异（ErbLoader.cs:1020-1025 仅 ParserMediator.Warn）。
- ecd/Difference.md 明确记录了「退出时 COUNT +1（含 BREAK 退出）」的行为，与源码一致：REND 在判定是否继续之前就先增量。
- REND 的步长判定保留了负步长分支，这是与 `FOR`/`NEXT` 共用实现（REPEAT_Instruction 带 `fornext` 参数）所致；REPEAT 自身步长固定为 1。
