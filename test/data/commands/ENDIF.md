# ENDIF

- **类别**：命令
- **签名**：`ENDIF`
- **文档来源**：`ecd/docs/translation/Command.md` 无独立 `### ENDIF` 小节（IF 系语义在「循环·分支语法」节及 TRYC 等小节中以 IF～ENDIF 结构说明出现）；`Era-Chinese-Documentation` 在 `zh/ERB_File_Format.md`（「IF」小节，684 行起）给出四种 IF 模式，均以 `ENDIF` 结尾。

## 语义

结束由 `IF` 开始的条件分支结构。`ENDIF` 运行期不做任何事，只是结构的终点标记；装载期它把配对 `IF` 块内所有 `IF`/`ELSEIF`/`ELSE` 行的跳转目标统一设为自己，使得条件为假或已完成分支的执行流都落到 `ENDIF` 之后继续。`ENDIF` 必须与最近的未闭合 `IF` 配对，否则报解析警告。可以嵌套。

## 用法

### `ENDIF`

无参数。

```erb
IF A == 1
    PRINTL A 是 1
ELSE
    PRINTL A 不是 1
ENDIF
PRINTL 无论哪个分支都会执行到这里
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:240` → `new ENDIF_Instruction(), METHOD_SAFE`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3204`（`ENDIF_Instruction`）；装载期链接在 `Runtime/Script/Loader/ErbLoader.cs:1128`（`case FunctionCode.ENDIF`）

```text
装载期（ErbLoader）:
    遇到 ENDIF:
        ifLine = nestStack 栈顶
        若栈空或栈顶不是 IF 行:
            发出解析警告（UnexpectedEndif），跳过
        否则:
            对 ifLine.IfCaseList 中的每一行
                （IF 自身、各 ELSEIF、各 ELSE）
                把它们的 JumpTo 设为本 ENDIF 行
            弹出 nestStack

运行期（ENDIF_Instruction.DoInstruction）:
    空方法体：什么都不做，顺序流自然通过

（跳到 ENDIF 的来源:
   - IF_Instruction: 所有条件为假且无 ELSE 时默认跳 ENDIF
   - ELSEIF/ELSE 行被顺序执行到时: 无条件 JumpTo(ENDIF)
   - 标志 flag = FLOW_CONTROL | PARTIAL | FORCE_SETARG，
     运行期本身无动作）
```

## 备注

- `ENDIF` 与 `ENDSELECT`（`Runtime/Script/Statements/FunctionIdentifier.cs:244`）、`DO`（`:252`）、`ENDCATCH`（`:365`）、`ENDFUNC`（`:370`）共用 `ENDIF_Instruction`，该类的 `DoInstruction` 为空方法体，上述五者的运行期动作都是空操作；跳转全部由装载期配对写在别的行上。
- ecd 文档描述：用 `GOTO`/`TRYGOTO` 直接跳入 IF 块内部时，会执行到下一个 `ELSEIF`/`ELSE`/`ENDIF` 之前，然后从 `ENDIF` 下一行继续——正是这些边界行的运行期动作决定的。
- `IF` 未闭合（缺 `ENDIF`）或 `ENDIF` 无配对 `IF` 都只在装载期警告；警告级别不同（缺配对为错误级 2，`ELSE` 后再 `ELSEIF` 为警告级 1）。
