# ENDSELECT

- **类别**：命令
- **签名**：`ENDSELECT`
- **文档来源**：`ecd/docs/translation/Command.md`（「循环·分支语法」节 `### SELECTCASE <式>`、`### ENDSELECT`）；`Era-Chinese-Documentation` 未收录（zh 各文档 grep 无 ENDSELECT 独立说明）。

## 语义

结束由 `SELECTCASE` 开始的多分支结构。`SELECTCASE`～`CASE`～`CASEELSE`～`ENDSELECT` 是与 Visual Basic 同名的分支语法：`ENDSELECT` 是整个结构（以及最后一个分支）的终点。装载期它把 `SELECTCASE` 行及所有 `CASE`/`CASEELSE` 行的跳转目标设为自己，并校验各 `CASE` 条件式与 `SELECTCASE` 表达式的类型一致。运行期 `ENDSELECT` 本身是空操作（`ENDIF_Instruction`），顺序流自然通过。与 `switch` 不同，不会从 `CASE` 顺序落到下一个 `CASE`。

## 用法

### `ENDSELECT`

无参数，位于 `SELECTCASE` 块末尾。

```erb
SELECTCASE X
    CASE 1
        PRINTL X is 1.
    CASE 2,3
        PRINTL X is 2 or 3.
    CASE 10 TO 20
        PRINTL X 在 10 以上 20 以下。
    CASE IS <= 30
        PRINTL X 在 30 以下。
    CASEELSE
        PRINTL 不符合任何条件。
ENDSELECT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:244` → `new ENDIF_Instruction(), METHOD_SAFE | EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3204`（`ENDIF_Instruction`，空操作）；装载期链接与校验在 `Runtime/Script/Loader/ErbLoader.cs:1170`（`case FunctionCode.ENDSELECT`）；运行期分支求值在同目录 `Runtime/Script/Statements/Instraction.Child.cs:3278`（`SELECTCASE_Instruction.DoInstruction`，类在 3271 行）

```text
装载期（ErbLoader，case FunctionCode.ENDSELECT）:
    selectLine = nestStack 栈顶
    若栈空或栈顶不是 SELECTCASE（且 SelectcaseStack 为空）:
        警告（UnexpectedEndselect），跳过
    若栈顶不是 SELECTCASE 但 SelectcaseStack 非空:
        逐层弹栈并警告（InstructionNotClosed：
                    跨越未闭合指令关闭 SELECTCASE）
        （弹出 SELECTCASE 与 nestStack 顶层，防止一个 ENDSELECT
          对应两个 SELECTCASE）
    正常路径:
        弹出 nestStack 与 SelectcaseStack
        selectLine.JumpTo = 本 ENDSELECT 行
        对 selectLine.IfCaseList 中每一行:
            该行.JumpTo = 本 ENDSELECT 行
            （若该行是 CASEELSE 或出错行则不再校验）
            检查 CASE 条件式数量（0 个 → 警告 MissingArg）
            对每个 CaseExpression:
                若其操作数类型与 SELECTCASE 表达式类型不一致
                    → 警告（NotMatchCaseTypeAndSelectcaseType）

运行期（ENDIF_Instruction.DoInstruction）:
    空方法体：什么都不做

（实际进入哪个分支由 SELECTCASE_Instruction 决定:
   求值 SELECTCASE 表达式（整数或字符串），
   按 IfCaseList 顺序对每个 CASE 的 CaseExps 调 GetBool 匹配
   （短路求值：命中即停），命中则跳到该 CASE 行；
   全不命中则跳到 CASEELSE 行（若无 CASEELSE 则跳 ENDSELECT 行，
   即 caseJumpto 初始值））
```

## 备注

- `ENDSELECT` 与 `ENDIF`、`DO`、`ENDCATCH`、`ENDFUNC` 共用 `ENDIF_Instruction`，运行期为空操作。
- ecd 文档「用 GOTO 直接跳入分支内部时，会执行到下一个 CASE、CASEELSE 或 ENDSELECT 为止，然后从 ENDSELECT 之后继续」：CASE/CASEELSE 行运行期是 `ELSEIF_Instruction`（无条件跳 ENDSELECT），ENDSELECT 为空操作，与文档一致。
- `SELECTCASE` 未闭合（缺 ENDSELECT）或跨指令闭合都会在装载期警告；实现还额外处理了「一个 ENDSELECT 对应两个 SELECTCASE」的错误写法（ecd 文档未提及）。
