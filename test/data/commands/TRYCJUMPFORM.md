# TRYCJUMPFORM

- **类别**：命令
- **签名**：
  - `TRYCJUMPFORM <FORM格式文本> (, 参数1, 参数2……)`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系2 (TRYC-CATCH-ENDCATCH)」→「TRYCJUMPFORM」；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

`TRYJUMPFORM` 对应的 TRYC 系版本：与 `JUMP` 相同的函数跳转，函数名用 FORM 格式文本给出（可 `{}` 插值、`%…%` 展开并可带参数）；目标函数不存在时不报错，而是转入 `CATCH`～`ENDCATCH` 分支执行失败处理。

TRYC 系结构要求：命令之后到 `CATCH` 之前为成功路径（可省略），`CATCH` 到 `ENDCATCH` 之间为失败处理，可嵌套；函数存在时执行到 `CATCH` 之前会跳到 `ENDCATCH` 的下一行。

## 用法

### `TRYCJUMPFORM <FORM格式文本> (, 参数1, 参数2……)`
- `<FORM格式文本>`：展开后即目标函数名（不带 `@`）。
- `参数1, 参数2……`：按目标函数声明的形参依次传入。
```erb
TRYCJUMPFORM KOJO_{NO:TARGET}_EXTRA_SCENE
  ;函数存在时执行到这里后跳到 ENDCATCH 之后
CATCH
  PRINTL 展开后的函数名不存在，进入失败处理
ENDCATCH
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:349` → `new CALL_Instruction(true, true, true, true), EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，form=true, isJump=true, isTry=true, isTryCatch=true）

```text
构造（CALL_Instruction(form=true, isJump=true, isTry=true, isTryCatch=true)）:
    参数构造器 = SP_CALLFORM（FORM 格式字符串实参 + 自定义函数实参列表）
    flag = FLOW_CONTROL | FORCE_SETARG | IS_JUMP | IS_TRY | IS_TRYC | PARTIAL
    # IS_TRYC | PARTIAL 使解析器把本行（TRYC 块首行）纳入 TRYC～CATCH～ENDCATCH 结构，
    #   并在解析成功后填好 func.JumpToEndCatch = 配对的 CATCH 行

静态解析阶段 SetJumpTo():
    非常量（FORM 文本通常如此）→ useCallForm = true，运行期解析；
    恰为常量时与 TRYCJUMP 相同：查到则缓存 JumpTo / 实参转换，查不到静默放行。

运行期 DoInstruction():
    若实参是常量: 取静态缓存的 call / labelName / arg
    否则: labelName = FORM 文本求值展开
          call = CalledFunction.CallFunction(按名查找)
    若 call == null:
        # TRYCJUMPFORM：isTry = true，不抛"函数未定义"
        若 func.JumpToEndCatch != null（本行作为 TRYC 块首行时为配对的 CATCH 行）:
            state.JumpTo(func.JumpToEndCatch)   # 失败 → 跳到 CATCH 行；JumpTo 的落点是目标行的下一行，
                                                # 故实际从 CATCH 的下一行开始执行失败处理分支
        返回
    call.IsJump = true                          # 与 JUMP 相同：顶替当前函数上下文
    若 arg == null: 按目标函数声明转换实参，失败 → 抛 CodeEE
    state.IntoFunction(call, arg, exm)
```

## 备注

- ecd 文档对 TRYCJUMPFORM 仅一句话"与 TRYJUMPFORM 对应的 TRYC 系版本"；具体行为需结合 TRYC 系总述与 TRYJUMPFORM 小节理解，示例为补写。
- zh 文档未收录本命令。
