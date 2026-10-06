# CALLFORM

- **类别**：命令
- **签名**：
  - `CALLFORM <FORM格式文本>(, 参数1, 参数2……)`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO 系」→「CALLFORM `<FORM格式文本>` (, 参数1, 参数2……)」（用法示例见 `JUMPFORM` 小节）；`Era-Chinese-Documentation/docs/Difference.md`（函数名含 `{}`/`%` 的兼容性差异）、`docs/Function_and_Preprocessor.md`（参数指定）。

## 语义

与 `CALL` 相同，但函数名可以像 `PRINTFORM` 那样写成带格式的字符串（`{表达式}`、`%字符串表达式%`），先展开格式文本得到函数名，再调用该函数并在其返回后回到调用处。典型用法是按编号拼接函数名，如 `CALLFORM KOJO_{NO:TARGET}_{SELECTCOM}`。

`CALLFORM` 可以指定参数（遵循被调函数的声明）。目标函数不存在时抛 CodeEE；要静默处理请用 `TRYCALLFORM`，要捕获请用 `TRYCCALLFORM`。

## 用法

### `CALLFORM <FORM格式文本>(, 参数1, 参数2……)`
- `<FORM格式文本>`：格式字符串，展开后为函数名（不带 `@`）。
- `参数1, 参数2……`：按被调函数声明的形参依次传入。
```erb
CALLFORM KOJO_{NO:TARGET}_{SELECTCOM}
PRINTFORML 返回值：{RESULT}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:344`（`new CALL_Instruction(true, false, false, false), EXTENDED`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，构造参数 `form=true, isJump=false, isTry=false, isTryCatch=false`）

```text
指令类 CALL_Instruction(form=true, ...):
    form=true → 参数构造器 SP_CALLFORM（函数名为 FORM 格式文本）
    flag = FLOW_CONTROL | FORCE_SETARG | EXTENDED

    SetJumpTo(...):
        若 func.Argument.IsConst == false（FORM 文本含插值，编译期定不了名字）:
            useCallForm = true          # 标记为运行期解析
            return

    DoInstruction(exm, func, state):    # 与 CALL 共用同一段执行逻辑
        spCallArg = func.Argument（SpCallArgment）
        # FORM 情况下 IsConst 通常为 false → 运行期路径：
        labelName = spCallArg.FuncnameTerm.GetStrValue(exm)   # 展开 {..} %..%
        call = CalledFunction.CallFunction(Process, labelName, func)
        若 call == null:
            # form=true 且 isTry=false → 直接抛错
            抛 CodeEE（"调用了不存在的函数" + labelName）
        call.IsJump = false             # 执行完返回调用处
        arg = call.ConvertArg(spCallArg.RowArgs)   # 失败抛 CodeEE
        state.IntoFunction(call, arg, exm)

CallFunction(...):
    同 CALL：事件函数 → 抛 CodeEE；#FUNCTION → 抛 CodeEE；
    不存在 → 返回 null（此处表现为报错）。
```

## 备注

- `CALL` 与 `CALLFORM` 是同一个 `CALL_Instruction` 类的两个构造变体，仅参数构造器（`SP_CALL` vs `SP_CALLFORM`）与 `EXTENDED` 标志不同；执行逻辑完全共用（见 `CALL.md` 的完整伪代码）。
- ecd 文档的 CALLFORM 小节本身只有一句「与 CALL 相同，但可以用带格式的字符串指定函数名」，示例放在 `JUMPFORM` 小节；文档与源码一致。
- zh 文档差异点：`Difference.md` 明确「Eramaker 中如果函数名称包含 `{}` 或 `%`，对 `CALLFORM` 的调用将不工作」，Emuera 已支持——即 CALLFORM 本身是 Emuera 对 Eramaker 的扩展（故标 `EXTENDED`）。
