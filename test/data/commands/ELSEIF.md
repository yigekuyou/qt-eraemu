# ELSEIF

- **类别**：命令
- **签名**：`ELSEIF <数值表达式>`（另有变体 `CASE <CASE条件式>`、`CASEELSE`，共用同一实现类，见备注）
- **文档来源**：`ecd/docs/translation/Command.md` 无独立 `### ELSEIF` 小节（分支语义在「循环·分支语法」节及 TRYC、SELECTCASE 等小节中以 IF 系说明出现）；`Era-Chinese-Documentation` 在 `zh/ERB_File_Format.md`（「IF」小节，684 行起）给出 IF~ELSEIF~ENDIF 模式。

## 语义

`IF`～`ENDIF` 条件分支结构中的「否则如果」分支。`IF` 的条件为假时依次求值各 `ELSEIF` 的 `<数值表达式>`，第一个不为 0（真）的 `ELSEIF` 分支被执行，其后的 `ELSEIF`/`ELSE` 全部跳过；全部为假时落到 `ELSE`（若有）。`ELSEIF` 必须位于 `IF` 与 `ENDIF` 之间，前面不能是 `ELSE`。条件表达式为数值型，0 为假、非 0 为真。

## 用法

### `ELSEIF <数值表达式>`

- `<数值表达式>`：为真（非 0）时执行本分支到下一个 `ELSEIF`/`ELSE`/`ENDIF` 之前的代码。

```erb
IF A > 0
    PRINTL A 是正数
ELSEIF A < 0
    PRINTL A 是负数
ELSE
    PRINTL A 是 0
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:239` → `new ELSEIF_Instruction(FunctionArgType.INT_EXPRESSION)`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3187`（`ELSEIF_Instruction`）；分支求值在 `IF_Instruction.DoInstruction`（同文件约 3223 行起），装载期链接在 `Runtime/Script/Loader/ErbLoader.cs:1113-1125`（`case FunctionCode.ELSEIF`/`ELSE` 追加进 `IfCaseList`）、`:1128-1139`（`case FunctionCode.ENDIF` 为 `IfCaseList` 各行统一设 `JumpTo`）

```text
装载期（ErbLoader）:
    遇到 ELSEIF:
        取 nestStack 栈顶 ifLine；若不是 IF 行 → 警告并跳过
        若 ifLine.IfCaseList 最后一项已是 ELSE → 警告（ELSE 之后不允许 ELSEIF）
        把本行追加到 ifLine.IfCaseList
    遇到 ENDIF:
        把 IfCaseList 中所有行（含本行）的 JumpTo 统一设为 ENDIF 行

运行期:
    ELSEIF_Instruction.DoInstruction:
        state.JumpTo(func.JumpTo)   ;无条件跳到配对的 ENDIF
        （顺序执行落到这里意味着前面的条件都为假、
          本分支的代码应被跳过）

    实际进入哪个分支由 IF_Instruction 决定:
        ifJumpto = ENDIF 行（默认）
        按 IfCaseList 顺序遍历:
            行出错 → 跳过
            行是 ELSE → ifJumpto = 该 ELSE 行，结束遍历
            否则求值该行条件（常量则取 ConstInt，
                          否则取 Term.GetIntValue）
            非 0（真）→ ifJumpto = 该行，结束遍历
        若 ifJumpto 不是 IF 自身 → state.JumpTo(ifJumpto)
```

## 备注

- 本命令与 `ELSE`（VOID）、`CASE`（CASE 参数类型）、`CASEELSE`（VOID，EXTENDED）共用同一个 `ELSEIF_Instruction` 类；`CASE`/`CASEELSE` 的条件匹配不走这里的数值求值，而由 `SELECTCASE_Instruction` 用 `CaseExpression.GetBool` 处理。
- 文档（zh）说明 0 为假、非 0 为真，与实现 `value != 0` 一致。
- ecd 文档提到：用 `TRYGOTO` 等直接跳入 IF 块内部时，会顺序执行到下一个 `ELSEIF`/`ELSE`/`ENDIF` 之前（这些行的运行期动作都是跳到 ENDIF），然后从 `ENDIF` 下一行继续——这正是 ELSEIF 运行期只是无条件跳转的原因。
