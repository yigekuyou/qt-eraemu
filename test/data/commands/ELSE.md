# ELSE

- **类别**：命令
- **签名**：`ELSE`
- **文档来源**：`ecd/docs/translation/Command.md` 中没有独立的 `### ELSE` 小节，语义散见于「循环·分支语法」节及 `REUSELASTLINE` 示例；`Era-Chinese-Documentation` 在 `zh/ERB_File_Format.md`（「IF」小节，684 行起）给出 IF~ELSEIF~ELSE~ENDIF 四种模式。

## 语义

`IF`～`ENDIF` 条件分支结构中「否则」分支的起点。当前面所有 `IF`/`ELSEIF` 条件都不成立时，从 `ELSE` 的下一行开始执行，直到 `ENDIF` 为止。一个 `IF` 块最多只能有一个 `ELSE`，且必须位于所有 `ELSEIF` 之后；`ELSE` 后不能再写 `ELSEIF`（运行时/解析时会警告）。`ELSE` 必须与最近的未闭合 `IF` 配对，否则报解析警告。

## 用法

### `ELSE`

无参数。位于 `IF`（及可选的若干 `ELSEIF`）之后、`ENDIF` 之前。

```erb
IF A == 1
    PRINTL A 是 1
ELSEIF A == 2
    PRINTL A 是 2
ELSE
    PRINTL A 既不是 1 也不是 2
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:238` → `new ELSEIF_Instruction(FunctionArgType.VOID)`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3187`（`ELSEIF_Instruction`）；跳转目标在装载期由 `Runtime/Script/Loader/ErbLoader.cs:1113-1114`（`case FunctionCode.ELSEIF / case FunctionCode.ELSE`）与 `:1128-1139`（`case FunctionCode.ENDIF`）建立

```text
装载期（ErbLoader，构建 IfCaseList 与 JumpTo）:
    遇到 ELSE:
        取 nestStack 栈顶 ifLine
        若 ifLine 不存在或不是 IF:
            发出解析警告（无效的 ELSE），跳过
        若 ifLine.IfCaseList 链表最后一项已是 ELSE:
            发出警告（ELSE 后又出现 ELSE/ELSEIF）
        把本行追加到 ifLine.IfCaseList 末尾
    遇到 ENDIF:
        对 ifLine.IfCaseList 中的每一行（IF 自身、各 ELSEIF、ELSE）
            把它们的 JumpTo 统一设为该 ENDIF 行
        弹出 nestStack

运行期（ELSEIF_Instruction.DoInstruction，ELSE 共用）:
    直接 state.JumpTo(func.JumpTo)   ;无条件跳到配对的 ENDIF
    （即：执行流只有在前面的 IF/ELSEIF 条件全部为假、顺序落到 ELSE 时
      才会真正执行 ELSE 与 ENDIF 之间的代码；
      而真正决定从哪个分支进入的是 IF_Instruction 遍历 IfCaseList
      求值各 ELSEIF 条件、遇到 ELSE 直接把跳转目标定为 ELSE 行）
```

## 备注

- ecd 文档没有 ELSE 独立小节（仅 IF 小节缺省，分支语义在 SELECTCASE/TRYC 等小节中以「与 IF～ELSEIF～ELSE～ENDIF 一样」形式提及），主要语义参考了 zh 文档的 IF 小节。
- 实现上 ELSE 与 ELSEIF、CASE、CASEELSE 共用同一个 `ELSEIF_Instruction` 类，仅构造参数（实参类型）不同；`ELSE` 与 `ENDIF` 之间的代码块本身没有运行时指令，全部由 IF 的跳转逻辑控制。
- `SIF` 后面不能跟 `ELSE`（SIF 只控制一行，PARTIAL 类指令跟在 SIF 后会被警告）。
