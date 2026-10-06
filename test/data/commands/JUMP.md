# JUMP

- **类别**：命令
- **签名**：
  - `JUMP <函数名>`
  - `JUMP <函数名>, <参数1>, <参数2>,……`（向自制函数传参）
- **文档来源**：`ecd/docs/translation/ERB_Statements.md`「跳转语句」；`ecd/docs/translation/Difference.md`「`JUMP` 的行为」；`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系」选择器小节（无独立 JUMP 小节）；zh 套件 `zh/Difference.md`「# JUMP的行为」可交叉核对。

## 语义

跳转到指定的函数（自制函数/事件函数等），**不记住跳转来源**：在跳转目标中执行 `RETURN`，效果与在 `JUMP` 所在函数中直接 `RETURN` 相同（回到再上一层的调用者，而不是回到 `JUMP` 处）。

- 指定的函数不存在时出错终止（TRY 系如 `TRYJUMP` 才不报错）。
- 从 `CALL` 调用的函数里也可以 `JUMP` 出去（Eramaker 中禁止，Emuera 允许——见 `Difference.md`）。
- 可以像 `CALL` 一样向目标函数传参，格式同自制函数的参数指定。
- `JUMP` 的目标是函数名，跳到函数内标签用 `GOTO`；带格式字符串的函数名用 `JUMPFORM`。

## 用法

### `JUMP <函数名>{, <参数1>, <参数2>……}`
- `<函数名>`：要跳转到的函数名。
- `<参数N>`：传给目标函数的实参，个数与类型须匹配目标函数的 `#FUNCTION` 参数声明。
```erb
;跳到 @FOO，之后 @FOO 中的 RETURN 相当于这里直接 RETURN
JUMP FOO
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:339`（`new CALL_Instruction(false, true, false, false)`——与 CALL 共用 `CALL_Instruction`，`form=false`（普通字符串函数名）、`isJump=true`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，flag = `FLOW_CONTROL | FORCE_SETARG | IS_JUMP`）

```text
解析期 SetJumpTo（函数名是常量时）:
    call = CalledFunction.CallFunction(Process, labelName, func)
    若 call == null 且本指令不是 TRY 系:
        FunctionoNotFoundName = labelName    # 报告“函数不存在”，出错
    否则若 call != null:
        func.JumpTo = call.TopLabel          # 记下目标函数，静态绑定
        目标 Depth 未设 → 设为 currentDepth + 1
        callArg.UDFArgument = call.ConvertArg(原始参数)   # 预转换实参，失败则警告

执行期 DoInstruction:
    spCallArg = (SpCallArgment)func.Argument
    若 spCallArg.IsConst:
        call = 解析期绑定的 callFunc；labelName、arg 取自常量参数
    否则:
        labelName = spCallArg.FuncnameTerm.GetStrValue(exm)   # 运行期求值函数名
        call = CalledFunction.CallFunction(Process, labelName, func)
    若 call == null:
        若 !isTry: 抛出 CodeEE（“函数 {labelName} 未定义”）   # JUMP 不容错
        （TRY 系则跳到配对的 CATCH 行（落点为 CATCH 的下一行）进入失败处理，或直接返回——JUMP 不会走到这里）
    call.IsJump = isJump          # ← JUMP 与 CALL 的本质区别：
                                  #   帧照样压入，但 RETURN 时若 called.IsJump 为真，
                                  #   会立刻弹出该帧并向外层继续传播 Return（回到来源函数的调用者），
                                  #   即 JUMP 行的下一行不会被恢复执行
    若 arg == null:
        arg = call.ConvertArg(spCallArg.RowArgs, out errMes)   # 实参转换，失败抛 CodeEE
    state.IntoFunction(call, arg, exm)    # 进入目标函数执行
```

## 备注

- 文档（`Difference.md`）：Eramaker 无法从 `CALL` 调用的函数中 `JUMP` 出去，Emuera 允许；「在 `JUMP` 的目标中 `RETURN`，与在 `JUMP` 的来源函数中 `RETURN` 行为相同」——对应源码 `call.IsJump = true`（`Process.State.Return` 中 `if (called.IsJump)` 分支会弹出该帧并继续向外层 `Return(ret)`，不恢复到 JUMP 行的下一行）。
- `Difference.md` 中「试图用 JUMP 调用一个通过 CALL 调用的函数」是**Eramaker 的输出**（该节以「Eramaker 输出／Emuera 输出」对照的形式给出，Emuera 的输出是正常执行完并回到 `@FOOBAR`）。Emuera 源码中没有这条检查——`CALL_Instruction` 与 `IntoFunction`（`Runtime/Script/Process.State.cs:446`）都不校验"目标函数是否正被 CALL 调用"。
- ecd `Command.md` 没有 JUMP 独立小节，只有 CALL·JUMP·GOTO 系选择器与 TRY 系变体说明；zh 的 `Command.md` 亦无，`zh/Difference.md` 与 ecd `Difference.md` 内容一致（JUMP 行为、`JUMP BAR` 示例）。
- `JUMP` 与 `GOTO` 的区别：`JUMP` 跳函数（`FLOW_CONTROL`、可传参、丢弃当前函数上下文），`GOTO` 跳同一函数内的标签（`METHOD_SAFE`）。
