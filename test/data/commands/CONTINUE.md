# CONTINUE

- **类别**：命令
- **签名**：`CONTINUE`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md`「FOR」「DO」小节中对 `CONTINUE` 的说明；`Era-Chinese-Documentation/docs/ERB_File_Format.md`「REPEAT～REND」段落提及

## 语义

循环中断指令。在 `REPEAT～REND`、`FOR～NEXT`、`WHILE～WEND`、`DO～LOOP` 四种循环中使用，跳过本次循环中 `CONTINUE` 之后的剩余部分，回到循环判定处：

- `REPEAT`/`FOR`：先把循环变量增加一个步长，再检查是否达到终止值；未达到则回到循环开头，已达到则退出循环。
- `WHILE`：重新求值 `WHILE` 的条件表达式，非 0 则回到循环开头，为 0 则退出。
- `DO`：重新求值 `LOOP` 的条件表达式（注意 `DO` 的判定行在 `CONTINUE` 之后），非 0 则回到 `DO`，为 0 则退出。

若无对应循环而执行到 `CONTINUE`（或循环结构异常），会抛出内部错误。`BREAK` 与之相对，直接退出整个循环。

## 用法

### 循环体内 `CONTINUE`
- 无参数；只能写在上述四种循环体内，否则解析/运行出错。
```erb
FOR X, 0, 10
	SIF X % 2 == 0
		CONTINUE      ; 偶数时跳过本次剩余处理
	PRINTFORML 奇数 {X}
NEXT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:254` → `new CONTINUE_Instruction()`（flag = METHOD_SAFE | FLOW_CONTROL）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3465`（`CONTINUE_Instruction`）

```text
DoInstruction(exm, func, state):
    jumpTo = (InstructionLine)func.JumpTo      // 所属循环的头部行（REPEAT/FOR/WHILE/DO）

    若 jumpTo 是 REPEAT 或 FOR:
        若 jumpTo.LoopCounter == null:
            // 循环变量不明（未经 REPEAT/FOR 正常进入循环，如 GOTO 跳入）
            // 沿袭 eramaker 仕様：视为循环结束，直接跳到循环出口
            state.JumpTo(jumpTo.JumpTo); return
        jumpTo.LoopCounter.ChangeValue(jumpTo.LoopStep, exm)   // 循环变量 += 步长（不检查溢出）
        counter = 循环变量当前值
        若 (步长>0 且 终止值>counter) 或 (步长<0 且 终止值<counter):
            state.JumpTo(func.JumpTo)          // 回循环开头（CONTINUE 的 JumpTo 即循环体起点）
        否则:
            state.JumpTo(jumpTo.JumpTo)        // 跳到 NEXT/REND 之后的行，退出循环

    若 jumpTo 是 WHILE:
        若 ((ExpressionArgument)jumpTo.Argument).Term.GetIntValue(exm) != 0:
            state.JumpTo(func.JumpTo)          // 条件仍真 → 回 WHILE
        否则:
            state.JumpTo(jumpTo.JumpTo)        // 退出

    若 jumpTo 是 DO:
        tFunc = jumpTo.JumpTo                  // 即配对的 LOOP 行
        若 tFunc.IsError: 抛出 CodeEE(tFunc.ErrMes, tFunc.Position)   // 判定行有错时在此暴露
        若 LOOP 的条件表达式 != 0: state.JumpTo(jumpTo)   // 回 DO
        否则:                      state.JumpTo(tFunc)    // 跳到 LOOP（其后），退出

    以上都不是 → 抛出 ExeEE("CONTINUE命令异常")   // 不在任何循环中
```

## 备注

- `REPEAT`/`FOR` 分支里 `CONTINUE` 会先执行"循环变量加步长"，这一点与 `BREAK` 相同（BREAK 在 WHILE/DO 分支不加）；ecd 文档只笼统说"可用 CONTINUE 中断"，具体跳转语义需看源码。
- `DO` 是唯一判定行位于 `CONTINUE` 之后的循环，因此源码特判了 `LOOP` 行错误的情况。
- ecd 文档「DO」小节提到"执行 CONTINUE 时如果 LOOP 的条件不满足，会直接退出 LOOP"，与实现一致；zh 文档仅提及 `REPEAT～REND` 中的 `CONTINUE`。
