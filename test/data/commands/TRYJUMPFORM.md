# TRYJUMPFORM

- **类别**：命令
- **签名**：
  - `TRYJUMPFORM <FORM格式文本> (, 参数1, 参数2……)`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系」→「TRYJUMPFORM」；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

`TRYJUMP` 的 FORM 版：与 `JUMP` 相同的函数跳转，但函数名可以像 `PRINTFORM` 那样用 `{}` 插值、`%…%` 展开表达式的格式字符串给出；且当最终函数名对应的函数不存在时不会报错，而是什么都不做、继续执行下一行。可以像 `TRYJUMP` 一样给目标函数传参数。

失败时静默跳过；若需要失败时进入 `CATCH` 分支，请改用 `TRYCJUMPFORM`。

## 用法

### `TRYJUMPFORM <FORM格式文本> (, 参数1, 参数2……)`
- `<FORM格式文本>`：展开后即目标函数名（不带 `@`）。与 `CALLFORM KOJO_{NO:TARGET}_{SELECTCOM}` 同理。
- `参数1, 参数2……`：按目标函数声明的形参依次传入。
```erb
TRYJUMPFORM KOJO_{NO:TARGET}_EXTRA_SCENE
PRINTL 目标函数不存在，继续到这里
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:345` → `new CALL_Instruction(true, true, true, false), EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，form=true, isJump=true, isTry=true, isTryCatch=false）

```text
构造（CALL_Instruction(form=true, isJump=true, isTry=true, isTryCatch=false)）:
    参数构造器 = SP_CALLFORM（FORM 格式字符串实参 + 可选的自定义函数实参列表）
    flag = FLOW_CONTROL | FORCE_SETARG | IS_JUMP | IS_TRY

静态解析阶段 SetJumpTo():
    FORM 文本含插值时通常不是编译期常量:
        useCallForm = true（运行期展开 FORM 再按名查找）
        返回
    （若恰为常量字符串，处理同 TRYJUMP：查到则缓存 JumpTo 与实参转换结果，
      查不到且为 TRY 系则静默放行）

运行期 DoInstruction():
    若实参是常量: 直接取静态缓存的 call / labelName / arg
    否则: labelName = FORM 文本求值展开得到的字符串
          call = CalledFunction.CallFunction(按名查找)
    若 call == null:
        # TRYJUMPFORM：isTry = true，不抛"函数未定义"
        若 func.JumpToEndCatch != null（TRYJUMPFORM 恒为 null，见 TRYJUMP 备注）→ 跳到配对的 CATCH 行
        否则直接返回（什么都不做）
    call.IsJump = true                          # 与 JUMP 相同：顶替当前函数上下文
    若 arg == null: 按目标函数声明转换实参，失败 → 抛 CodeEE
    state.IntoFunction(call, arg, exm)
```

## 备注

- 文档对 TRYJUMPFORM 的描述只说"TRYJUMP 的 FORM 版、失败不出错"；实现上与 TRYJUMP 完全共用 `CALL_Instruction`，仅构造参数 `form` 不同。
- ecd 文档在 CALL·JUMP·GOTO 系各小节均未给出 TRYJUMPFORM 的独立示例，此处示例仿照 `JUMPFORM` 小节的 `CALLFORM KOJO_{NO:TARGET}_{SELECTCOM}` 风格。
- zh 文档未收录本命令。
