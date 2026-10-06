# CASEELSE

- **类别**：命令
- **签名**：
  - `CASEELSE`
- **文档来源**：`ecd/docs/translation/Command.md`「多路分支：SELECTCASE」→「CASEELSE」；`Era-Chinese-Documentation` 套件未收录（其 `Command.md` 无 SELECTCASE/CASEELSE 专节）。

## 语义

`SELECTCASE`～`CASE`～`CASEELSE`～`ENDSELECT` 多路分支语法的兜底分支：当 `SELECTCASE` 的值不符合任何 `CASE` 条件时，跳入 `CASEELSE` 之下、`ENDSELECT` 之上的行执行。无参数、无条件式。可省略；省略且全部 `CASE` 未命中时直接落到 `ENDSELECT` 之后继续。位置必须在所有 `CASE` 之后、`ENDSELECT` 之前。

与 `IF`～`ELSE` 相同，用 `GOTO` 等直接跳入 `SELECTCASE` 块内部时，执行会像流过 `CASE` 行一样，在下一个 `CASE`、`CASEELSE` 或 `ENDSELECT` 处截住，然后从 `ENDSELECT` 之后继续。

## 用法

### `CASEELSE`
- 无参数。
```erb
SELECTCASE X
  CASE 1
    PRINTL X is 1.
  CASE 3
    PRINTL X is 3.
  CASEELSE
    PRINTL X is not 1 or 3.
ENDSELECT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:243`（`new ELSEIF_Instruction(FunctionArgType.VOID), EXTENDED`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3187`（`ELSEIF_Instruction`，与 `ELSE`/`CASE` 共用）；兜底选择逻辑在 `SELECTCASE_Instruction.DoInstruction`（同文件 3271 行起）

```text
# CASEELSE 行本身的执行体（ELSEIF_Instruction.DoInstruction，参数 VOID）：
    state.JumpTo(func.JumpTo)
    # CASEELSE 是分支锚点之一：被 SELECTCASE 匹配选中时跳入其下；
    # 顺序流入时跳到下一个锚点（同 CASE/ELSEIF 的截断行为）。

# SELECTCASE_Instruction.DoInstruction 中的 CASEELSE 处理：
    对 func.IfCaseList 顺序遍历（CASE 与 CASEELSE 混排的行列表）:
        若 line.FunctionCode == CASEELSE:
            caseJumpto = line           # 不再检查任何条件，直接选它
            break                       # 即便后面还有 CASE 也以 CASEELSE 为准
        否则按 CaseExpression 检查 CASE 条件（见 CASE.md）
    state.JumpTo(caseJumpto)
    # 遍历结束仍无命中且块内没有 CASEELSE → caseJumpto 保持为 ENDSELECT
```

## 备注

- 实现上 CASEELSE 按 `IfCaseList` 中的出现位置参与匹配循环：源码遍历到 CASEELSE 即选中（`break`），因此 CASEELSE 必须写在所有 CASE 之后——这与文档描述的语法位置一致；写在前面的 CASEELSE 会在检查它之后的 CASE 之前被选中（两套文档均未讨论这种病态写法）。
- `CASEELSE` 与 `ELSE` 完全共用 `ELSEIF_Instruction(FunctionArgType.VOID)` 实例形式，语义差异只由外层是 `IF` 还是 `SELECTCASE` 的匹配循环决定。
- zh 套件无本命令专节，无交叉核对来源。
