# CASE

- **类别**：命令
- **签名**：
  - `CASE <CASE条件式>(, <CASE条件式>, <CASE条件式> ……)`
  - 条件式形式：`<值>`、`<起始值> TO <结束值>`、`IS <运算符> <数值表达式>`，以及用逗号分隔的多个条件式
- **文档来源**：`ecd/docs/translation/Command.md`「多路分支：SELECTCASE」→「CASE `<CASE条件式>`(, `<CASE条件式>`, `<CASE条件式>` ……)`」；`Era-Chinese-Documentation` 套件的 `Command.md` 未收录（其中无 SELECTCASE/CASE 专节），仅 `Custom_Variable.md` 有无关的 `#DIM SELECTCASE` 词条。

## 语义

`SELECTCASE`～`CASE`～`CASEELSE`～`ENDSELECT` 多路分支语法的分支条件行。`SELECTCASE <式>` 求值一次后，从上到下依次检查每个 `CASE` 的条件式，命中第一个满足的条件即跳入其下执行（不会再落向下个 `CASE`，也不能用 `BREAK` 跳入 `ENDSELECT`）。条件式有三种写法：

- 单个值：SELECTCASE 的值与它相等（数值比较或字符串比较）时命中；
- `<起始值> TO <结束值>`：值在「左边以上、右边以下」时命中；右边小于左边时永不命中；
- `IS <运算符> <数值表达式>`：把 SELECTCASE 的值作为 IS 左侧，按运算符比较，如 `CASE IS <= 30`。

一个 `CASE` 可用逗号写多个条件式，从左到右短路求值，一旦满足剩余条件不再求值。`SELECTCASE` 为字符串时，`CASE` 条件式也必须是字符串表达式（字符串也支持 `TO` 范围，按字典序）。

## 用法

### `CASE <值>`
匹配单个值。
### `CASE <值1>, <值2>(, ……)`
匹配其中任意一个。
### `CASE <起始值> TO <结束值>`
匹配闭区间。右边小于左边时永不命中。
### `CASE IS <运算符> <数值表达式>`
按比较运算符（`<=`、`>`、`==` 等）匹配。`IS` 与 `TO` 必须按此形式书写，不能写成 `30 < IS` 或 `(10 TO 20) || (30 TO 40)`。
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
  CASE 40, 5*10 TO 6*10, IS >= 100
    PRINTL X 是 40、50~60、100 以上的某一个。
  CASEELSE
    PRINTL X 不符合以上任何条件。
ENDSELECT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:242`（`new ELSEIF_Instruction(FunctionArgType.CASE), EXTENDED`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3187`（`ELSEIF_Instruction`，CASE 时仅作跳转桩）；真正的匹配逻辑在 `SELECTCASE_Instruction`（同文件 3271 行起）与 `Runtime/Script/Statements/CaseExpression.cs:12`（`CaseExpression`）

```text
# CASE 行本身的执行体（ELSEIF_Instruction.DoInstruction）：
    state.JumpTo(func.JumpTo)
    # CASE 是分支锚点：当 SELECTCASE 匹配到本 CASE 时跳到这里；
    # 顺序执行流入 CASE 行时（如用 GOTO 跳进分支内部），
    # 会像 ELSEIF 一样跳到下一个 CASE/CASEELSE/ENDSELECT。

# 匹配逻辑（由 SELECTCASE_Instruction.DoInstruction 执行）：
    selectValue = SELECTCASE 参数（求值一次）
    对 func.IfCaseList（SELECTCASE 块内的 CASE/CASEELSE 行列表）顺序遍历:
        若 line.FunctionCode == CASEELSE: caseJumpto = line; 结束
        caseArg = (CaseArgument)line.Argument
        对 caseArg.CaseExps 中的每个 CaseExpression 顺序检查:   # 短路求值
            命中则 caseJumpto = line; 跳出（goto casefound）
    state.JumpTo(caseJumpto)      # 无命中且无 CASEELSE → ENDSELECT

CaseExpression.GetBool(Is, exm):    # 单个条件式的判定
    CaseType == To:  返回 LeftTerm <= Is <= RightTerm     # 闭区间，含字符串字典序
    CaseType == Is:  返回 (Is <运算符> LeftTerm) != 0      # IS 左侧是 SELECTCASE 值
    Normal:          返回 (LeftTerm == Is)                # 单值相等
```

## 备注

- 文档「与 `switch` 不同，不会从 `CASE` 顺序落到下一个 `CASE`，也不能用 `BREAK` 跳入 `ENDSELECT`」与实现一致：CASE 行执行体是 `JumpTo(下一锚点)`，天然带 fall-through 抑制。
- 文档「`TO` 在右边小于左边时永不命中」「短路求值」与 `GetBool`（`left <= Is && Is <= right`）及 CaseExps 的顺序遍历一致。
- `CASE` 与 `CASEELSE`、`ELSEIF`、`ELSE` 共用 `ELSEIF_Instruction` 类（`Runtime/Script/Statements/FunctionIdentifier.cs:242/243` 等），仅参数类型不同（`FunctionArgType.CASE` vs `VOID`）；`SELECTCASE`/`IF` 的匹配循环负责解释它们。
- zh 套件无 CASE/SELECTCASE 专节（其 `Command.md` 只有 Print 系），语义无交叉核对来源。
