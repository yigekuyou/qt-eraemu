# IF

- **类别**：命令
- **签名**：
  - `IF <条件式>`（复合语句头，必须以 `ENDIF` 结束，中间可配 `ELSEIF`、`ELSE`）
- **文档来源**：`ecd/docs/translation/ERB_Compound_Statements.md`「条件分支：IF」；`ecd/docs/translation/ERB_Statements.md`「命令语句 / 单行条件语句：SIF」；`Era-Chinese-Documentation` 套件未收录 IF 命令专节。

## 语义

条件分支复合语句。`IF` 后接一个数值（整数）条件式，从上往下逐个求值本 `IF` 块内各 `ELSEIF` 的条件：第一个成立（值非 0）的分支获得执行权；若全都不成立且有 `ELSE`，则执行 `ELSE` 分支；若全不成立且无 `ELSE`，则直接跳到 `ENDIF` 之后。整个块必须以 `ENDIF` 结束。`ELSEIF` 可以有任意多个，`ELSE` 只能有一个且必须是最后一个分支。

`IF` 与 `SIF` 不同：`SIF` 只作用于下一行，`IF`～`ENDIF` 可包裹任意多行。条件式结果按「非 0 为真」判断。

## 用法

### `IF <条件式>` ～ `ELSEIF <条件式>` ～ `ELSE` ～ `ENDIF`
- `<条件式>`：整数表达式，非 0 即为真。
- `ELSEIF` 分支：可省略、可重复；仅当之前所有条件都不成立时才求值。
- `ELSE` 分支：可省略；前面条件全部不成立时执行。
- `ENDIF`：必须存在，标志块结束。
```erb
IF X > 100
  PRINTL X 大于 100
ELSEIF X > 50
  PRINTL X 大于 50 但不超过 100
ELSE
  PRINTL X 不超过 50
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:237`（`new IF_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3216`（`IF_Instruction`，flag = `METHOD_SAFE | FLOW_CONTROL | PARTIAL | FORCE_SETARG`）

```text
IF 行在解析期已把 func.JumpTo 指向对应的 ENDIF 行，
func.IfCaseList 按顺序保存本块的行：装载期先 `AddFirst` 把 IF 行自身放在首位，随后依次追加各 ELSEIF / ELSE 行（`Runtime/Script/Loader/ErbLoader.cs:1041-1042`、`:1125`）。

DoInstruction(exm, func, state):
    jumpTo = func.JumpTo            # 默认跳到 ENDIF（即全部分支不执行）
    对 func.IfCaseList 中的每个 line：
        若 line 是错误行 → 跳过
        若 line.FunctionCode == ELSE:
            jumpTo = line           # ELSE 分支无条件命中
            跳出循环
        state.CurrentLine = line    # 让报错能定位到具体 ELSEIF 行
        若 line.Argument.IsConst:
            value = line.Argument.ConstInt
        否则:
            value = line.Argument.Term.GetIntValue(exm)   # 求值该 ELSEIF 条件
        若 value != 0:
            jumpTo = line
            跳出循环
    若 jumpTo != func 自身:
        state.JumpTo(jumpTo)        # 跳到被选中分支行（继续执行其体）或 ENDIF
    # 命中分支后的执行会沿分支行继续走到下一个 ELSEIF/ELSE/ENDIF，
    # 由解析器保证分支行本身也会跳过后续分支（ELSEIF/ELSE 亦为跳转指令）。
```

## 备注

- ecd 详解站把 IF 放在复合语句文档（`ERB_Compound_Statements.md`）而非 `Command.md`，`Command.md` 与 `ERB_Commands.md` 的命令表中均无 IF 条目；zh 套件无对应小节。
- 源码中 ELSEIF/ELSE 也注册为 `ELSEIF_Instruction`（`Runtime/Script/Statements/FunctionIdentifier.cs:238-239`），真正的一次性分支选择逻辑集中在 `IF_Instruction`：它在遇到第一个真分支时一次性求值完毕，之后通过跳转让执行流转入该分支，从而保证后面的条件不再求值。
- 若某 `ELSEIF` 的条件表达式求值抛错，错误会定位到该 `ELSEIF` 行（源码注释：1.730 修复 ELSEIF 的错误被算到 IF 头上的问题）。
