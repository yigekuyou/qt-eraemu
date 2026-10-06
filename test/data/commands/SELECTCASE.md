# SELECTCASE

- **类别**：命令（复合分支结构头）
- **签名**：`SELECTCASE <式>`
- **文档来源**：`ecd/docs/translation/Command.md`（SELECTCASE / CASE / CASEELSE / ENDSELECT 小节）、`ecd/ERB_Compound_Statements.md`（多路分支）；`Era-Chinese-Documentation/docs/` 未收录对应小节

## 语义

`SELECTCASE`～`CASE`～`CASEELSE`～`ENDSELECT` 是与 Visual Basic 同名同语法的多路分支结构。对 `SELECTCASE` 的单一求值结果，从头到尾依次检查各 `CASE` 的条件，第一个命中的 `CASE` 之下的行被执行，然后直接跳到 `ENDSELECT` 之后继续（不会像 C 的 switch 那样顺序落空到下一个 CASE，也不能用 `BREAK` 跳出）。

如果没有任何 `CASE` 命中，则执行 `CASEELSE` 之下的行；没有 `CASEELSE` 时直接结束分支。

`CASE` 未命中即短路：同一 `CASE` 的多个条件从左到右依次求值，一旦有满足的条件，其余条件不再求值。用 `GOTO` 直接跳入分支内部时与 `IF` 系一样：执行到下一个 `CASE`、`CASEELSE` 或 `ENDSELECT` 为止，然后从 `ENDSELECT` 之后继续。

数值式与字符串式均可作为分支依据；对 `SELECTCASE` 传入字符串时，各 `CASE` 的条件式也必须是字符串式。

## 用法

### SELECTCASE <式> ～ CASE ～ CASEELSE ～ ENDSELECT

- `<式>`：数值或字符串表达式，作为分支依据（求值一次）。
- `CASE <值>`：单一值匹配。
- `CASE <值1>, <值2>, …`：逗号分隔的值列表，命中任一即可。
- `CASE <起始值> TO <结束值>`：范围匹配，仅当左端 ≤ 值 ≤ 右端时为真；右端小于左端时该条件永不成立。
- `CASE IS <运算符> <式>`：比较匹配，如 `IS <= 30`。`IS` 与 `TO` 必须写作上述形式，不能写 `30 < IS` 或 `(10 TO 20) || (30 TO 40)`。
- `CASEELSE`：以上皆不匹配时执行的分支。
- `ENDSELECT`：结束分支。

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
  CASE 40, 5 * 10 TO 6 * 10, IS >= 10 * 10
    PRINTL X 是 40、50~60、100 中的某一个。
  CASEELSE
    PRINTL X 不符合以上任何条件。
ENDSELECT
```

字符串分支：

```erb
SELECTCASE STR:0
  CASE "A"
    PRINTL 是 A
  CASEELSE
    PRINTL 不是 A
ENDSELECT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:241`（`new SELECTCASE_Instruction()`；同段注册 `CASE → ELSEIF_Instruction(CASE)`、`CASEELSE → ELSEIF_Instruction(VOID)`、`ENDSELECT → ENDIF_Instruction`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3271`（`SELECTCASE_Instruction`，`DoInstruction` 在 3278 行）

```text
SELECTCASE_Instruction:
    构造: ArgBuilder = EXPRESSION, 标记 METHOD_SAFE | EXTENDED | FLOW_CONTROL | PARTIAL | FORCE_SETARG
    // 解析期：func.IfCaseList 已按行序装好各 CASE/CASEELSE 行，func.JumpTo 指向 ENDSELECT

    DoInstruction(exm, func, state):
        caseJumpto = func.JumpTo          // 默认落点 = ENDSELECT（无 CASEELSE 时到此为止）
        selectValue = func.Argument.Term
        if selectValue.IsInteger:
            iValue = selectValue.GetIntValue(exm)
        else:
            sValue = selectValue.GetStrValue(exm)

        foreach line in func.IfCaseList:  // 按源码顺序扫描各 CASE 行
            if line.IsError: continue     // 有错的 CASE 行跳过
            if line.FunctionCode == CASEELSE:
                caseJumpto = line         // 扫到 CASEELSE 即为兜底分支，停止
                break
            caseArg = (CaseArgument)line.Argument
            state.CurrentLine = line      // 使 CASE 条件式在 CASE 行的上下文求值
            Is = (数值式 ? iValue : sValue)
            foreach caseExp in caseArg.CaseExps:      // 单个 CASE 的多个条件，短路求值
                if caseExp.GetBool(Is, exm):          // TO/IS/值/列表统一在此判定
                    caseJumpto = line
                    goto casefound                    // 命中即停，不再检查后面的 CASE
        casefound:
        state.JumpTo(caseJumpto)
        // 执行流随后从命中的 CASE 行（或 CASEELSE/ENDSELECT）顺序向下执行，
        // 各 CASE/CASEELSE/ENDSELECT 行本身还会按 IF/ELSEIF/ENDIF 语义再次裁剪流向
```

## 备注

- 执行机制上，`SELECTCASE` 在自身处完成全部条件判定并跳到命中的 `CASE` 行；`CASE`/`CASEELSE` 注册为 `ELSEIF_Instruction(CASE/VOID)`、`ENDSELECT` 注册为 `ENDIF_Instruction`，因此顺序向下流经这些行时不会再二次分支（与 IF/ELSEIF/ENDIF 同构）。源码中大量「チェック済み」注释表明解析期已保证 `IfCaseList` 与 `JumpTo` 必然有效。
- 文档（ecd Command.md）指出 `TO` 范围「左边以上、右边以下」；源码中该语义由 `CaseExpression.GetBool` 实现，SELECTCASE 本体不重复判断。
- `CASE` 行解析出错时（`line.IsError`）会被静默跳过、当作不存在——文档未提及此行为。
- `zh/` 文档套件没有 SELECTCASE 独立小节，无从交叉核对。
