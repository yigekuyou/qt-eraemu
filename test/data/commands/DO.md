# DO

- **类别**：命令（EE 扩展命令，循环流控制）
- **签名**：
  - `DO`
- **文档来源**：`ecd/docs/translation/Command.md`「循环·分支语法」小节（`### DO`/`### LOOP`）；zh 套件未收录 DO～LOOP 语法说明。

## 语义

`DO`～`LOOP` 是类似于 do～while 的循环语法：`DO` 标记循环体开头，`LOOP <数值表达式>` 结束循环体并在表达式非 0 时跳回 `DO` 继续循环。与 `WHILE`～`WEND` 不同，**循环体至少会执行一次**（条件在循环体末尾才判定）。

要点：
- 条件恒为真时成为无限循环，需要用 `BREAK` 退出；循环过长时 Emuera 会发出警告。
- 在循环内执行 `CONTINUE` 时会跳到 `LOOP` 处判定条件；若此时条件不满足，直接退出 `LOOP`（与 `WHILE` 的 `CONTINUE` 语义不同）。
- 用 `GOTO` 等指令直接跳入 `DO`～`LOOP` 内部时，到达 `LOOP` 后会像通常一样判定条件并跳回 `DO`。
- `DO` 自身执行时**不做任何事**（无条件进入循环体），条件判定完全由 `LOOP` 完成。

## 用法

### `DO ～ LOOP <数值表达式>`
- `DO`：无参数，标记循环体开头。
- `LOOP <数值表达式>`：循环末尾的判定；表达式非 0 时回到 `DO`，为 0 时从 `LOOP` 的下一行继续。
```erb
X = 0
DO
  X += 1
  PRINTFORML X = {X}
LOOP X < 3
;输出 X = 1、X = 2、X = 3（即使初始条件已不成立也先执行一次）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:252`（`new ENDIF_Instruction(), METHOD_SAFE | EXTENDED`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:217`）；配对语法关系在 `Runtime/Script/Statements/FunctionIdentifier.cs:492`（`funcParent[LOOP] = DO`）
- 实现：`DO` 本身复用 `ENDIF_Instruction`（空操作指令类）；循环回跳由 `LOOP` 侧完成——`LOOP_Instruction` 在 `Runtime/Script/Statements/Instraction.Child.cs:3564`；`DO`/`LOOP` 配对与跳转目标设定在 `Runtime/Script/Loader/ErbLoader.cs:1088`（`DO` 压栈）与 `:1228`（`LOOP` 出栈配对）；`CONTINUE` 对 DO 循环的特殊处理在 `Runtime/Script/Statements/Instraction.Child.cs:3504` 附近

```text
# DO 的执行期语义
ENDIF_Instruction.DoInstruction:      # DO 复用它
    什么都不做                          # 无条件进入循环体

# 载入期（ErbLoader）配对
    遇到 DO: 压入 nestStack
    遇到 LOOP:
        parentFunc = funcParent[LOOP] = DO
        若 nestStack 栈顶不是 DO: 警告（缺少对应的 DO）
        pairLine = 出栈（即 DO 行）
        LOOP.JumpTo = DO 行             # 条件成立时跳回这里
        DO.JumpTo   = LOOP 行           # 供 BREAK/CONTINUE 跳出到 LOOP 之后

# LOOP 的执行期语义（LOOP_Instruction）
    若 参数表达式求值 != 0:
        state.JumpTo(func.JumpTo)       # 跳回 DO，继续循环
    否则: 顺序执行 LOOP 的下一行

# CONTINUE 在 DO 循环内（CONTINUE_Instruction，jumpTo 为 DO 时）
    tFunc = DO.JumpTo                   # 即对应的 LOOP 行
    若 tFunc 解析出错: 抛出 CodeEE（LOOP 行的错误信息）
    若 LOOP 条件求值 != 0: 跳回 DO      # 继续循环（落点为 DO 的下一行，即循环体开头）
    否则: 跳到 LOOP 行（落点为 LOOP 下一行）  # 直接退出循环（注意不是 LOOP.JumpTo，它指向 DO）
    # 注释原文：只有 DO 是判定行在 CONTINUE 之后，判定行本身出错时需要特殊处理

# BREAK 在 DO 循环内
    跳到 DO.JumpTo（LOOP 行）→ LOOP 当次不回跳，从下一行继续
```

## 备注

- 文档与源码唯一的偏差点：索引/注册处写 `DO → new ENDIF_Instruction()`，看起来像 IF 语法，实际是复用「空操作」指令类实现「DO 行本身无事可做」；真正的循环语义全在 `LOOP` 与载入期的配对逻辑中。
- `CONTINUE` 会**重新求值 LOOP 的条件**，这与文档「执行 CONTINUE 时如果 LOOP 的条件不满足，会直接退出 LOOP」一致；而 `WHILE` 循环的 `CONTINUE` 是重新求值 `WHILE` 的条件，方向相反。
- zh 套件（Era-Chinese-Documentation）的 ERB 格式文档未介绍 DO～LOOP 语法。
