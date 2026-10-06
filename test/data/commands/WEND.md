# WEND

- **类别**：命令（循环语法）
- **签名**：`WEND`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md` → `### WEND`（"结束由 `WHILE` 开始的循环"）；`Era-Chinese-Documentation/docs/`（zh/Command.md）未收录。

## 语义

`WHILE`～`WEND` 循环的终点标记。执行到 `WEND` 时会重新求值对应 `WHILE` 的条件表达式：条件不为 0 则跳回循环体开头继续循环，为 0 则结束循环、从 `WEND` 之后继续。用 `GOTO` 等指令直接跳入 `WHILE`～`WEND` 内部时，读到 `WEND` 后会像通常一样回到 `WHILE`（重新判定条件）。`WHILE` 与 `WEND` 必须成对出现，否则解析期报"缺少对应的 WHILE"警告。

## 用法

### WEND
无参数。与最近的未闭合 `WHILE` 配对。

```erb
X = 0
WHILE X < 3
  PRINTFORL X = {X}
  X += 1
WEND
;输出 X = 0 / X = 1 / X = 2 后退出循环
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:251`（`new WEND_Instruction()`）；配对规则 `Runtime/Script/Statements/FunctionIdentifier.cs:465`（`funcMatch[WHILE] = "WEND"`）、`:491`（`funcParent[WEND] = WHILE`）
- 配对与跳转设置：`Runtime/Script/Loader/ErbLoader.cs:1227-1241`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3549`（`WEND_Instruction`）

```text
// 解析期（ErbLoader）
读到 WEND 时:
    parentFunc <- WHILE
    若 嵌套栈为空 或 栈顶不是 WHILE: 发出警告"缺少对应的 WHILE"
    否则:
        pairLine <- 弹出栈顶（对应的 WHILE 行）
        WEND.JumpTo  <- WHILE 行          // 循环回边
        WHILE.JumpTo <- WEND 行           // 条件为假时的出口（WHILE 用）

// 执行期
函数 DoInstruction(exm, func, state):      // func = WEND 行
    jumpTo <- (InstructionLine)func.JumpTo // 即对应的 WHILE 行
    若 jumpTo 的条件表达式（WHILE 的参数）取整值 != 0:
        state.JumpTo(WHILE 行)             // 主循环随后 ShiftNextLine，从 WHILE 下一行（循环体开头）继续
    // 为 0 时顺序落到 WEND 下一行，循环结束

// 主循环语义：state.JumpTo(line) 只把当前行设为 line，
// 下一轮迭代会 ShiftNextLine，因此实际从 line 的下一行继续执行。
```

## 备注

- 文档说"读到 WEND 后会像通常一样回到 WHILE"；源码实现是 WEND 直接重新求值 WHILE 的条件表达式，为真时跳到 WHILE 行的下一行（循环体），为假时顺序继续——语义等价，但并非真的重新执行 WHILE 指令。
- `WHILE`/`WEND` 均带 `FLOW_CONTROL | PARTIAL` 标志；`BREAK`/`CONTINUE`（`Runtime/Script/Loader/ErbLoader.cs:1091-1111`）会把最近的 `REPEAT`/`FOR`/`WHILE`/`DO` 作为配对目标。
- ecd 文档还警告：条件恒真时成为无限循环，循环过长时 Emuera 会发出警告（对应运行期的 `checkInfiniteLoop` 机制）。
