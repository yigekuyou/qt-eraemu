# TRYGOTOLIST

- **类别**：命令
- **签名**：
  - `TRYGOTOLIST`
  - `FUNC <字符串>`（仅可出现在 `TRYGOTOLIST`～`ENDFUNC` 之间）
  - `ENDFUNC`（结束候选列表）
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系2 (TRYC-CATCH-ENDCATCH)」→「TRYGOTOLIST」「FUNC」；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

与 `FUNC`～`ENDFUNC` 配合，按顺序尝试跳转到多个 `$` 标签中的第一个存在的标签：依次求值各 `FUNC` 行给出的标签名，找到第一个存在的标签就跳转过去，不再尝试后续候选；一个都不存在时，跳到 `ENDFUNC` 的下一行继续，不报错。

`TRYLIST` 系～`ENDFUNC` 之内不能书写这些语法以外的内容。与 `TRYJUMPLIST`（调用函数）不同，本命令的候选是当前函数内的 `$` 标签。

## 用法

### `TRYGOTOLIST ～ FUNC ～ ENDFUNC`
```erb
TRYGOTOLIST
  FUNC 标签A
  FUNC 标签B
ENDFUNC
PRINTL 全部候选标签都不存在时执行到这里
$标签A
```
- `FUNC` 行：每行一个候选标签名（可以是 FORM 格式文本）。`FUNC` 允许带参数列表，但 GOTO 目标是标签，参数在此无意义。

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:368` → `argb[FunctionArgType.VOID], EXTENDED | FLOW_CONTROL | PARTIAL | IS_TRY`（注意：与 TRYJUMPLIST 相比没有 IS_JUMP 标志；候选行 `FUNC` 注册于同文件 `:369`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:859-877`（`doFlowControlFunction` 的 `case FunctionCode.TRYGOTOLIST:`）

```text
运行期 doFlowControlFunction()（TRYGOTOLIST 分支）:
    jumpto = null
    对 func.callList 中每个 FUNC 候选行 iLine 依次尝试:
        若 iLine.Argument == null → ArgumentParser.SetArgumentTo(iLine)  # 惰性解析候选行实参
        funcName = ((SpCallArgment)iLine.Argument).FuncnameTerm.GetStrValue(exm)
        jumpto = state.CurrentCalled.CallLabel(this, funcName)           # 在当前函数内查 $ 标签
        若 jumpto != null → 跳出循环                                     # 第一个存在的标签即命中
    若 jumpto == null:
        state.JumpTo(func.JumpTo)        # 全部不存在 → 跳到 ENDFUNC 的下一行继续
    否则:
        state.JumpTo(jumpto)             # 跳到命中的标签处
```

## 备注

- 与 TRYJUMPLIST 分支不同：TRYGOTOLIST 用 `state.CurrentCalled.CallLabel`（查 `$` 标签）而非 `CalledFunction.CallFunction`（查函数），且无 `IsJump`、无实参转换，因此候选标签不存在时不会因参数不匹配报错。
- ecd 文档对 TRYJUMPLIST/TRYGOTOLIST 的描述完全相同（都写"跳转到多个标签"），对 TRYGOTOLIST 而言与源码相符；TRYJUMPLIST 一侧的差异已在其文档备注中记录。
- 源码中存在 `if (jumpto != null) break` 后统一 `JumpTo` 的写法，语义即"第一个存在的候选生效"。
- zh 文档未收录本命令。
