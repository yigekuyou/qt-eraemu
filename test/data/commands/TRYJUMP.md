# TRYJUMP

- **类别**：命令
- **签名**：
  - `TRYJUMP <字符串> (, 参数1, 参数2……)`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系」→「TRYJUMP」；`Era-Chinese-Documentation` 套件未收录本命令（仅 `Function_and_Preprocessor.md:192` 顺带提到 TRYCALL）。

## 语义

与 `JUMP` 相同的函数跳转指令，但指定的函数不存在时不会报错，而是什么都不做、直接继续执行下一行。与 `JUMP` 一样，转移后原函数上下文被目标函数"顶替"：目标函数中的 `RETURN` 不会回到本命令处，而是回到更外层。`TRYJUMP` 与 `TRYCALL` 一样可以指定传给目标函数的参数（规则见「自制函数中的参数指定」）。

`TRYJUMP` 是 `TRYC` 系（TRYC-CATCH）之前的基础 TRY 指令：失败时静默跳过；若需要失败时进入 `CATCH` 分支，请改用 `TRYCJUMP`。

## 用法

### `TRYJUMP <字符串> (, 参数1, 参数2……)`
- `<字符串>`：目标函数名（不带 `@`）。可以是常量字符串，也可以是字符串表达式（非常量时在运行期求值并解析）。
- `参数1, 参数2……`：按目标函数声明的形参依次传入。
```erb
;存在则跳转过去，不存在则继续往下执行
TRYJUMP SHOW_EXTRA_SCENE, TARGET
PRINTL 函数不存在，或跳转返回后到达这里
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:341` → `new CALL_Instruction(false, true, true, false), EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，form=false, isJump=true, isTry=true, isTryCatch=false）

```text
构造（CALL_Instruction(form=false, isJump=true, isTry=true, isTryCatch=false)）:
    参数构造器 = SP_CALL（字符串实参 + 可选的自定义函数实参列表）
    flag = FLOW_CONTROL | FORCE_SETARG | IS_JUMP | IS_TRY     # isJump/isTry 各置一位；无 IS_TRYC、无 PARTIAL

静态解析阶段 SetJumpTo():
    若函数名实参不是编译期常量:
        useCallForm = true（改为运行期按名字查找）
        返回
    labelName = 常量函数名
    call = CalledFunction.CallFunction(Process, labelName, 本行)
    若 call == null 且本指令是 TRY 系:           # TRYJUMP 即如此
        什么都不做（不记录"函数未定义"错误名）
    若 call != null:
        func.JumpTo = call.TopLabel；设置调用深度
        若 TopLabel 本身有错 → 标记本行为错误
        把实参列表按目标函数声明转换成 UDFArgument；转换失败仅发警告

运行期 DoInstruction():
    若实参是常量: call、labelName、arg 直接取静态解析缓存
    否则: labelName = 字符串表达式求值；call = CallFunction(按名查找)
    若 call == null:
        若非 TRY 系 → 抛 CodeEE("函数@xx未定义")
         # TRYJUMP：isTry = true
        若 func.JumpToEndCatch != null → 跳到配对的 CATCH 行（落点为 CATCH 的下一行，即失败处理块开头）
        否则直接返回（什么都不做）
    call.IsJump = true                          # 顶替当前函数上下文，RETURN 不回到这里
    若 arg == null: 按目标函数声明转换实参，失败 → 抛 CodeEE
    state.IntoFunction(call, arg, exm)          # 进入目标函数
```

## 备注

- 文档将 TRYJUMP 描述为"与 JUMP 相同但失败不出错"；实现上它与 CALL/JUMP 共用 `CALL_Instruction`，仅以 `(isJump, isTry)` 两个标志区分，语义一致。
- `JumpToEndCatch` 字段只由解析器写到 TRYC 系块的块首行与 `CATCH` 行上（`Runtime/Script/Loader/ErbLoader.cs:1256`、`:1267`）。普通 `TRYJUMP` 不带 IS_TRYC 标志、也不是 TRYC 系块首行，所以该字段恒为 null，失败时总是直接返回、继续下一行；失败跳入 `CATCH` 只有 `TRYCJUMP` 等 TRYC 系指令才有（此时 `JumpToEndCatch` = 配对的 `CATCH` 行）。
- zh 文档未收录本命令。
