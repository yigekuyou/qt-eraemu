# TRYGOTO

- **类别**：命令
- **签名**：
  - `TRYGOTO <字符串>`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系」→「TRYGOTO」；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

与 `GOTO` 相同的 `$` 标签跳转指令，但指定的标签不存在时不会报错，而是什么都不做、继续执行下一行。

用 `TRYGOTO` 直接跳入 `IF`～`ELSEIF`～`ELSE`～`ENDIF` 内时，会像通常一样执行到 `ELSEIF`、`ELSE`、`ENDIF` 之前，然后跳到 `ENDIF` 的下一行继续处理；直接跳入 `REPEAT`～`REND` 内时，会像通常一样执行到 `REND` 之前，然后忽略 `REND` 从下一行继续。这些行为与 `GOTO` 及其他 GOTO 系指令相同。

若需要失败时进入 `CATCH` 分支，请改用 `TRYCGOTO`。

## 用法

### `TRYGOTO <字符串>`
- `<字符串>`：目标 `$` 标签名（标签在函数内以 `$标签名` 定义）。可以是常量字符串或字符串表达式。
```erb
@SAMPLE
  TRYGOTO SKIP_PART
  PRINTL 标签不存在时也会执行到这里
  $SKIP_PART
  PRINTL 到达 SKIP_PART
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:357` → `new GOTO_Instruction(false, true, false), EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3746`（`GOTO_Instruction`，form=false, isTry=true, isTryCatch=false）

```text
构造（GOTO_Instruction(form=false, isTry=true, isTryCatch=false)）:
    参数构造器 = SP_CALL（字符串实参）
    flag = METHOD_SAFE | FLOW_CONTROL | FORCE_SETARG | IS_TRY

静态解析阶段 SetJumpTo():
    仅当标签名实参是编译期常量时处理：
        labelName = 常量标签名
        jumpto = LabelDictionary.GetLabelDollar(labelName, 所在函数)
        若 jumpto == null:
            若本指令非 TRY 系 → 发出"标签$xx未定义"解析警告
            否则（TRYGOTO）静默返回，func.JumpTo 保持 null
        否则若 jumpto.IsError → 发出"非法标签名"警告
        否则 func.JumpTo = jumpto
    （非常量时不做静态解析）

运行期 DoInstruction():
    若实参是常量:
        label = 常量标签名
        若 func.JumpTo != null → jumpto = func.JumpTo
        否则 → 直接返回（TRY 且标签不存在：什么都不做）
    否则:
        label = 字符串表达式求值
        jumpto = state.CurrentCalled.CallLabel(按名在当前函数内查找标签)
    若 jumpto == null:
        若非 TRY 系 → 抛 CodeEE("标签$xx未定义")
        # TRYGOTO：isTry = true
        若 func.JumpToEndCatch != null → 跳到配对的 CATCH 行（落点为 CATCH 的下一行，即失败处理块开头）
        否则返回（什么都不做；JumpToEndCatch 只写到 TRYC 系块首行与 CATCH 行，TRYGOTO 恒为 null）
    否则若 jumpto.IsError → 抛 CodeEE("非法标签名")
    否则 state.JumpTo(jumpto)                   # 跳到标签处
```

## 备注

- 文档详述了跳入分支/循环语法内的行为，这些是 GOTO 系共有的执行器语义（`Runtime/Script/Process.ScriptProc.cs` 的顺序执行器负责），不在 `GOTO_Instruction` 本身内。
- 源码 `GOTO_Instruction.DoInstruction` 中非 TRY 且标签不存在时抛 `CodeEE`，与文档"GOTO 报错、TRYGOTO 不报错"一致。
- zh 文档未收录本命令。
