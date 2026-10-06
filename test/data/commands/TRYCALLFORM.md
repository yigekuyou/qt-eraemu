# TRYCALLFORM

- **类别**：命令（Eramaker 无）
- **签名**：TRYCALLFORM `<FORM格式文本>` (, 参数1, 参数2……)
- **文档来源**：`ecd/docs/translation/Command.md`「### TRYCALLFORM」（另见「### JUMPFORM」小节的用法示例）；Era-Chinese-Documentation 无对应小节

## 语义

与 `TRYCALL` 相同的「函数不存在时不出错」的调用指令，但函数名可以用像 `PRINTFORM` 一样的带格式字符串（FORM）书写，例如：

```erb
TRYCALLFORM KOJO_{NO:TARGET}_{SELECTCOM}
```

参数传递规则与 `CALLFORM`/`TRYCALL` 相同。指定的函数（格式化后求出的名字）不存在时什么都不做，继续执行下一行。

## 用法

### TRYCALLFORM `<FORM格式文本>` (, 参数1, 参数2……)
- 第 1 参数：FORM 格式的函数名文本，`{表达式}` 与 `%字符串变量%` 会被求值拼接成函数名。
- 之后的参数：传给被调函数的实参，按被调函数声明匹配。

```erb
;调用了 @SHOW_DATA_0（若存在），不存在则静默跳过
TRYCALLFORM SHOW_DATA_{SELECTCOM}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:346`（`new CALL_Instruction(true, false, true, false), EXTENDED`；即 form=true, isJump=false, isTry=true, isTryCatch=false，flag = FLOW_CONTROL | FORCE_SETARG | IS_TRY）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，与 TRYCALL 共用同一类，仅 `form` 标志不同）；参数构造用 `FunctionArgType.SP_CALLFORM`（FORM 格式函数名 + 参数列表，存入 `SpCallArgment`，见 `Runtime/Script/Statements/Argument.cs:272`）

```text
与 TRYCALL 完全相同的实现（CALL_Instruction），区别仅有两点：
  1) 构造时 form = true → ArgBuilder = SP_CALLFORM
     第 1 参数按 FORM 格式字符串解析，运行期对 FuncnameTerm.GetStrValue(exm)
     求值得到函数名（{name}、%str% 均在表达式层展开）。
  2) 由于函数名含插值，编译期 func.Argument.IsConst 不成立
     （只有不含插值的纯字面量 FORM 文本才会被当作常量处理），
     SetJumpTo 直接 useCallForm = true，函数查找全部推迟到运行期：
       labelName = FORM 求值结果
       call = 按名查找
       若 call == null 且 isTry:
           若 func.JumpToEndCatch != null: 跳到配对的 CATCH 行
               （TRYCALLFORM 不是 TRYC 系块首行，该字段恒为 null，走不到这里）
           否则: 什么都不做（TRYCALLFORM 的行为）
       否则: call.IsJump = false；ConvertArg 失败抛 CodeEE；
             state.IntoFunction(call, arg, exm) 进入函数，之后返回。
```

## 备注

- ecd 文档的示例 `CALLFORM KOJO_{NO:TARGET}_{SELECTCOM}` 同样适用于 TRYCALLFORM，只需把指令名换掉。
- 与 `TRYCCALLFORM` 的区别是 `isTryCatch`：本指令没有 `CATCH`～`ENDCATCH`。
- 「函数名是 FORM」意味着编译期不做存在性检查与实参预检，全部在运行期进行；实参不匹配仍会抛 CodeEE，这一点与「函数不存在静默跳过」不同。
