# LOOP

- **类别**：命令
- **签名**：
  - `LOOP <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「DO～LOOP」相关小节（`### LOOP`）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

`DO`～`LOOP` 是类似 do～while 的循环语法：`DO` 开始循环体，`LOOP <数值表达式>` 结束循环体，并在 `<数值表达式>` 不为 0（真）时跳回 `DO` 继续循环。与 `WHILE`～`WEND` 不同，`DO`～`LOOP` 的循环体至少会执行一次。

执行 `CONTINUE` 时若 `LOOP` 的条件不满足，会直接退出循环。用 `GOTO` 等指令直接跳入 `DO`～`LOOP` 内部时，到达 `LOOP` 后若条件为真会照常回到 `DO`。条件始终为真会成为无限循环，需用 `BREAK` 退出。

## 用法

### `LOOP <数值表达式>`
- `<数值表达式>`：循环继续条件。不为 0 时跳回对应的 `DO`；为 0 时结束循环，从 `LOOP` 的下一行继续。
```erb
;至少输出一次，之后按条件重复
DO
  PRINTL once or more
  X = X - 1
LOOP X > 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:253`（`new LOOP_Instruction()`）；`DO` 在同文件 252 行注册为 `ENDIF_Instruction`（空指令）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3564`（`LOOP_Instruction`，参数 `INT_EXPRESSION`，标志 `METHOD_SAFE | EXTENDED | FLOW_CONTROL | PARTIAL | FORCE_SETARG`）

```text
case LOOP:
    expArg = (ExpressionArgument)func.Argument
    若 expArg.Term 求值 != 0:            # 条件为真
        state.JumpTo(func.JumpTo)        # JumpTo 在解析期指向对应的 DO 行
    # 条件为假时不跳转，自然落到 LOOP 的下一行（循环结束）
```

相关：`CONTINUE` 在 `DO`～`LOOP` 中的行为（`Runtime/Script/Statements/Instraction.Child.cs:3510` 附近）：找到所属的 `LOOP` 行，求值其条件表达式；为真则跳到 `DO`，为假则跳到 `LOOP` 之后（直接退出循环）；找不到所属循环则抛 `ExeEE`（CONTINUE 异常）。

## 备注

- 本仓库中 `DO` 本身注册为 `ENDIF_Instruction`（即「什么都不做」的空操作指令类，实现见 `Runtime/Script/Statements/Instraction.Child.cs:3204`；该类自身带 `FLOW_CONTROL | PARTIAL | FORCE_SETARG`，`DO` 注册时再附加 `METHOD_SAFE | EXTENDED`），循环的回跳完全由 `LOOP` 完成；这与文档「到达 LOOP 后回到 DO」的描述一致。
- 解析期约束：`funcParent[FunctionCode.NEXT] = FunctionCode.FOR` 同类的父子配对在 `Runtime/Script/Statements/FunctionIdentifier.cs:489-492` 维护（REND→REPEAT、NEXT→FOR、WEND→WHILE、LOOP→DO），保证嵌套配对正确。
- zh 套件未收录本命令。
