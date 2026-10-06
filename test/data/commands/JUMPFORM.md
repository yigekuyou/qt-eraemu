# JUMPFORM

- **类别**：命令
- **签名**：
  - `JUMPFORM <FORM格式文本>`
  - `JUMPFORM <FORM格式文本>, <参数1>, <参数2>……`（向自制函数传参）
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系」JUMPFORM 小节；zh 套件未收录本命令（`zh/` 目录中无提及）。

## 语义

与 `JUMP` 相同，但可以像 `PRINTFORM` 等一样用带格式的字符串（FORM）指定要跳转的函数名。跳转不保留返回点：在目标函数中 `RETURN` 与在 `JUMPFORM` 所在函数中 `RETURN` 行为相同。

- 指定的函数不存在时出错（容错版是 `TRYJUMPFORM`）。
- 可以指定参数，用法同 `CALL`/`JUMP` 的自制函数参数指定。

典型用法（文档示例）：`JUMPFORM KOJO_{NO:TARGET}_{SELECTCOM}`——按当前角色与指令动态拼出口上函数名。

## 用法

### `JUMPFORM <FORM格式文本>{, <参数1>, <参数2>……}`
- `<FORM格式文本>`：FORM 格式字符串，展开后为目标函数名。
- `<参数N>`：传给目标函数的实参，须与目标函数声明匹配；可省略。
```erb
;根据 TARGET 与 SELECTCOM 动态跳转到对应的口上函数
JUMPFORM KOJO_{NO:TARGET}_{SELECTCOM}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:343`（`new CALL_Instruction(true, true, false, false), EXTENDED`——与 CALL/JUMP/CALLFORM 共用 `CALL_Instruction`，`form=true`、`isJump=true`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，flag = `FLOW_CONTROL | FORCE_SETARG | IS_JUMP`；参数 builder 为 `SP_CALLFORM`）

```text
解析期 SetJumpTo:
    若 func.Argument 不是常量（函数名含 FORM 展开式，通常如此）:
        useCallForm = true    # 放弃静态绑定，运行期再解析
        返回

执行期 DoInstruction:
    spCallArg = (SpCallArgment)func.Argument
    若 spCallArg.IsConst（罕见的纯字面量）:
        call = 解析期绑定的 callFunc
        labelName = spCallArg.ConstStr
        arg = spCallArg.UDFArgument
    否则:
        labelName = spCallArg.FuncnameTerm.GetStrValue(exm)   # ← FORM 字符串在此求值
        call = CalledFunction.CallFunction(Process, labelName, func)
    若 call == null:
        若 !isTry: 抛出 CodeEE（“函数 {labelName} 未定义”）
    call.IsJump = isJump            # isJump = true：与 JUMP 相同，RETURN 时不恢复到本行（帧被弹出并向外层传播）
    若 arg == null:
        arg = call.ConvertArg(spCallArg.RowArgs, out errMes)   # 实参转换，失败抛 CodeEE
    state.IntoFunction(call, arg, exm)
```

## 备注

- 文档明言「与 `JUMP` 相同，但可以……用带格式的字符串指定函数名」，源码证实：与 `JUMP` 仅差注册时 `form=true`（`SP_CALLFORM` 参数 builder），运行逻辑完全共用。
- 由于函数名通常含 `{}` 展开式，解析期 `SetJumpTo` 直接置 `useCallForm = true` 走运行期绑定路径——即函数名在每次执行时求值，文档「函数不存在时报错」也发生在运行期。
- ecd `Command.md` 中 JUMPFORM 小节还提示「`JUMPFORM` 与 `CALLFORM` 可以指定参数」，指同文档「函数与预处理指令」的「自制函数中的参数指定」。
- zh 套件未收录本命令。
