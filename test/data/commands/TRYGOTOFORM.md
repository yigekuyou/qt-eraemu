# TRYGOTOFORM

- **类别**：命令
- **签名**：
  - `TRYGOTOFORM <FORM格式文本>`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系」→「TRYGOTOFORM」；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

`TRYGOTO` 的 FORM 版：与 `GOTO` 相同的 `$` 标签跳转，但标签名可以像 `PRINTFORM` 那样用格式字符串（`{}` 插值、`%…%` 展开）给出；且当展开后的标签不存在时不会报错，而是什么都不做、继续执行下一行。

用 `TRYGOTOFORM` 直接跳入循环、分支语法内时的行为，与 `TRYGOTO`/`GOTO` 相同（执行到 `ENDIF`/`REND` 之前后跳出）。若需要失败时进入 `CATCH` 分支，请改用 `TRYCGOTOFORM`。

## 用法

### `TRYGOTOFORM <FORM格式文本>`
- `<FORM格式文本>`：展开后即目标 `$` 标签名。
```erb
@SAMPLE
  TRYGOTOFORM LABEL_{SELECTCOM}
  PRINTL 展开后的标签不存在时执行到这里
  $LABEL_0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:359` → `new GOTO_Instruction(true, true, false), EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3746`（`GOTO_Instruction`，form=true, isTry=true, isTryCatch=false）

```text
构造（GOTO_Instruction(form=true, isTry=true, isTryCatch=false)）:
    参数构造器 = SP_CALLFORM（FORM 格式字符串实参）
    flag = METHOD_SAFE | FLOW_CONTROL | FORCE_SETARG | IS_TRY

静态解析阶段 SetJumpTo():
    仅当实参恰为编译期常量字符串时才做静态标签解析（同 TRYGOTO）；
    FORM 文本含插值时不是常量，静态阶段不做任何事。

运行期 DoInstruction():
    若实参是常量:
        label = 常量标签名
        若 func.JumpTo != null → jumpto = func.JumpTo
        否则 → 直接返回（TRY 且标签不存在：什么都不做）
    否则（FORM 文本）:
        label = FORM 文本求值展开得到的字符串
        jumpto = state.CurrentCalled.CallLabel(按名在当前函数内查找标签)
    若 jumpto == null:
        # TRYGOTOFORM：isTry = true，不抛"标签未定义"
        若 func.JumpToEndCatch != null → 跳到配对的 CATCH 行（落点为 CATCH 的下一行，即失败处理块开头）
        否则返回（什么都不做；JumpToEndCatch 只写到 TRYC 系块首行与 CATCH 行，TRYGOTOFORM 恒为 null）
    否则若 jumpto.IsError → 抛 CodeEE("非法标签名")
    否则 state.JumpTo(jumpto)
```

## 备注

- ecd 文档对 TRYGOTOFORM 的说明引用「TRYGOTO 及循环·分支语法相关章节」，本身不含示例；上面示例为补写。
- FORM 版静态解析（`SetJumpTo`）对非常量实参不做检查，标签解析全部发生在运行期。
- zh 文档未收录本命令。
