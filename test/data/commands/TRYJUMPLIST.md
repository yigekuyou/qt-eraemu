# TRYJUMPLIST

- **类别**：命令
- **签名**：
  - `TRYJUMPLIST`
  - `FUNC <字符串> (, 参数1, 参数2……)`（仅可出现在 `TRYJUMPLIST`/`TRYCALLLIST`～`ENDFUNC` 之间）
  - `ENDFUNC`（结束候选列表）
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系2 (TRYC-CATCH-ENDCATCH)」→「TRYJUMPLIST」「FUNC」；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

与 `FUNC`～`ENDFUNC` 配合，按顺序尝试跳转到多个函数中的第一个存在的函数：依次求值各 `FUNC` 行给出的函数名（可以是 FORM 格式文本），找到第一个存在的函数就带参数跳转过去（与 `TRYJUMP` 一样是顶替上下文的 JUMP 语义，不保留返回点），不再尝试后续候选；一个都不存在时，跳到 `ENDFUNC` 的下一行继续，不报错。

`TRYLIST` 系～`ENDFUNC` 之内不能书写这些语法以外的内容。

## 用法

### `TRYJUMPLIST ～ FUNC ～ ENDFUNC`
```erb
TRYJUMPLIST
  FUNC 函数1, TARGET
  FUNC 函数2
ENDFUNC
PRINTL 全部候选都不存在时执行到这里
```
- `FUNC` 行：每行一个候选。`<字符串>` 为函数名（FORM 格式文本），其后可跟传给目标函数的参数。
- 第一个存在的候选被调用；其余候选不会被执行。

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:367` → `argb[FunctionArgType.VOID], EXTENDED | FLOW_CONTROL | PARTIAL | IS_JUMP | IS_TRY`（候选行 `FUNC` 注册于同文件 `:369`，`argb[FunctionArgType.SP_CALLFORM], EXTENDED | FLOW_CONTROL | PARTIAL | FORCE_SETARG`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:830-858`（`doFlowControlFunction` 的 `case FunctionCode.TRYCALLLIST:` 与 `case FunctionCode.TRYJUMPLIST:` 共用同一段）

```text
运行期 doFlowControlFunction()（TRYJUMPLIST 分支）:
    callto = null
    对 func.callList 中每个 FUNC 候选行 iLine 依次尝试:
        cfa = (SpCallArgment)iLine.Argument
        funcName = cfa.FuncnameTerm.GetStrValue(exm)      # FUNC 的字符串/FORM 文本求值
        callto = CalledFunction.CallFunction(this, funcName, func.JumpTo)
        若 callto == null → 继续下一个候选                  # 不存在：静默试下一个
        callto.IsJump = func.Function.IsJump()            # TRYJUMPLIST 注册带 IS_JUMP → true（JUMP 语义）
        args = callto.ConvertArg(cfa.RowArgs, out errMes) # 实参按目标函数声明转换
        若 args == null → 抛 CodeEE(errMes)                # 参数不匹配是硬错误，不走 TRY 逻辑
        state.IntoFunction(callto, args, exm)             # 进入目标函数
        return true                                       # 成功即结束，不再看后续候选
    所有候选都不存在:
        state.JumpTo(func.JumpTo)                         # 跳到 ENDFUNC 的下一行继续
```

## 备注

- **文档与源码差异**：ecd 文档把 TRYJUMPLIST 描述为"按顺序尝试跳转到多个标签中的第一个存在的标签"，与 TRYGOTOLIST 的描述一字不差；但源码中 TRYJUMPLIST 与 TRYCALLLIST 共用分支，调用的是函数（`CalledFunction.CallFunction` + `IntoFunction`），且带 IS_JUMP 标志（JUMP 语义）。按实现应理解为"跳转到第一个存在的**函数**"，文档此处措辞不准确。
- 实参转换失败（`ConvertArg` 返回 null）时直接抛 CodeEE，不会被 TRY 静默吞掉；"函数不存在"才会被跳过。
- zh 文档未收录本命令。
