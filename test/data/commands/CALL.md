# CALL

- **类别**：命令
- **签名**：
  - `CALL <函数名>(, 参数1, 参数2……)`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO 系」→「CALL·JUMP·GOTO 系命令辅助选择器」（`CALL` 为选择器基准行）及 `ecd ERB_File_Format.md`「函数」节（CALL 与 RETURN/RESULT）；`Era-Chinese-Documentation/docs/ERB_File_Format.md` 同段（含示例）。

## 语义

调用一个普通的（非事件、非式中）用户自定义函数 `@函数名`，执行完毕后返回调用处继续。被调函数执行到 `RETURN` 时，其参数按顺序保存进 `RESULT`（多个参数依次进入 `RESULT:0`、`RESULT:1`…）；若函数没有执行 `RETURN` 就结束，`RESULT` 为 0。参数传递遵循函数声明（`#DIM` 等可变参数与引用传递由函数头声明决定）。

目标函数不存在时抛 CodeEE 错误（TRY 系不报错、TRYC 系可被 `CATCH` 捕获，均由其他指令承担）。目标若是事件函数（可多重定义）则报错；目标若是 `#FUNCTION` 式中函数也报错（应改用 `CALLF`）。函数名中包含 `{}` 或 `%` 时请使用 `CALLFORM`。

## 用法

### `CALL <函数名>(, 参数1, 参数2……)`
- `<函数名>`：字符串常量，不带 `@`。对应已定义的 `@函数名`。
- `参数1, 参数2……`：按被调函数声明的形参依次传入；引用参数（`#DIMS REF` 等）传变量引用。
```erb
@EVENTFIRST
  CALL OPENING
  PRINTFORMW 开局函数执行的结果为{RESULT}。

@OPENING
  PRINTL 序章……
  RETURN 1
;CALL 返回后 RESULT = 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:340`（`new CALL_Instruction(false, false, false, false)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，构造参数 `form=false, isJump=false, isTry=false, isTryCatch=false`）；函数解析在 `Runtime/Script/Process.CalledFunction.cs:109`（`CalledFunction.CallFunction`）

```text
指令类 CALL_Instruction(form, isJump, isTry, isTryCatch):
    form=false → 参数构造器 SP_CALL（函数名为常量字符串）
    flag = FLOW_CONTROL | FORCE_SETARG
    # CALLFORM = 同类 (true,false,false,false)，另加 EXTENDED

    SetJumpTo(...)  # 编译期：参数为常量时解析函数名
        call = CalledFunction.CallFunction(Process, labelName, func)
        call == null 且非 TRY 系 → 记录 FunctionoNotFoundName（延迟报错）
        call != null → 绑定 func.JumpTo = call.TopLabel；
            callArg.UDFArgument = call.ConvertArg(RowArgs)  # 编译期检查实参

    DoInstruction(exm, func, state):
        spCallArg = func.Argument（SpCallArgment）
        若 spCallArg.IsConst:
            call = spCallArg.CallFunc（编译期缓存）; arg = spCallArg.UDFArgument
        否则:
            labelName = spCallArg.FuncnameTerm.GetStrValue(exm)  # CALLFORM 路径
            call = CalledFunction.CallFunction(Process, labelName, func)
        若 call == null:
            若非 isTry: 抛 CodeEE（"调用了不存在的函数" + labelName）
            否则若 func.JumpToEndCatch != null（TRYC 系块首行上 = 配对的 CATCH 行）:
                state.JumpTo(func.JumpToEndCatch)   # 跳到 CATCH 行，落点为 CATCH 的下一行（失败处理块开头）
            return                       # 普通 TRY 系（本行无 TRYC 结构，JumpToEndCatch 恒为 null）静默通过：
                                         # 什么都不做，继续下一行
        call.IsJump = isJump             # CALL 为 false → 执行完返回
        若 arg == null: arg = call.ConvertArg(RowArgs)；失败抛 CodeEE
        state.IntoFunction(call, arg, exm)   # 压入被调函数，记录返回地址

CallFunction(parent, label, retAddress):
    labelline = LabelDictionary.GetNonEventLabel(label)
    labelline == null:
        若同名事件函数存在 → 抛 CodeEE（不能用 CALL 调用事件函数）
        返回 null（调用方处理"函数不存在"）
    labelline.IsMethod（#FUNCTION）→ 抛 CodeEE（应使用 CALLF）
    返回 CalledFunction(TopLabel = labelline)
```

## 备注

- 文档与源码一致：`RESULT` 默认 0、`RETURN` 的参数保存到 `RESULT` 由 `RETURN` 指令类（`RETURN_Instruction` 调 `SetResultX`）负责，`CALL` 本身只负责进入函数（`IntoFunction` 不写 `RESULT`）。
- ecd 的选择器表把 `CALL` 归纳为：函数名形式=普通字符串、错误处理=目标不存在即报错、动作=CALL（执行后返回）、返回值处理=无 F 后缀（非式中函数）。`JUMP`/`GOTO`/`TRY` 系全部由同一个 `CALL_Instruction`/`GOTO_Instruction` 类通过构造参数复用（如 `TRYCALL = CALL_Instruction(false,false,true,false)`，`CALLFORM = CALL_Instruction(true,false,false,false)`）。
- zh 文档与 ecd 在语义上一致：zh 称「使用 `CALL` 语句调用的函数执行到 `RETURN` 后返回，`RETURN` 的参数会被保存在 `RESULT` 变量中」。源码中 `RETURN` 的参数类型为 `INT_ANY`，只经 `SetResultX` 写入 `RESULT` 数组，没有把返回参数写入 `RESULTS` 的路径；要让调用方拿到字符串结果，需由被调函数自行给 `RESULTS` 赋值（字符串返回值本身属于 `#FUNCTION` + `RETURNF` 的范畴）。
- 目标为事件函数的默认报错有兼容开关可关：开启「允许 CALL 事件函数」（`CompatiCallEvent`）后，`LabelDictionary` 会把事件函数的首个定义也登记进普通函数表，`CALL` 事件函数不再报错，且只执行最先定义的一个（忽略 `#PRI`/`#LATER`/`#SINGLE`）——对应 ecd `Compatibility.md`「事件函数可被 `CALL`」行的备注「可用兼容开关恢复」。
- 差异提示：`zh/Difference.md` 指出 Eramaker 时代若函数名含 `{}`/`%`，`CALLFORM` 不工作；Emuera 正常支持。
