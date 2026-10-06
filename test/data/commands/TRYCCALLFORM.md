# TRYCCALLFORM

- **类别**：命令
- **签名**：
  - `TRYCCALLFORM <FORM格式文本> (, 参数1, 参数2……)`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系2 (TRYC-CATCH-ENDCATCH)」→「TRYCCALLFORM」；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

`TRYCALLFORM` 对应的 TRYC 系版本：以 FORM 格式文本指定函数名调用 `@函数`（像 `CALL` 一样执行完返回，`RETURN` 的整数参数依次存入 `RESULT`，与 `CALL` 相同；`RETURN` 没有写 `RESULTS` 的路径），可以带参数。展开后的函数不存在时不报错，而是转入 `CATCH`～`ENDCATCH` 分支执行失败处理。

TRYC 系结构：命令之后到 `CATCH` 之前为成功路径（可省略），`CATCH` 到 `ENDCATCH` 之间为失败处理，可嵌套；函数存在时执行到 `CATCH` 之前会跳到 `ENDCATCH` 的下一行继续。

## 用法

### `TRYCCALLFORM <FORM格式文本> (, 参数1, 参数2……)`
- `<FORM格式文本>`：展开后即目标函数名（不带 `@`）。
- `参数1, 参数2……`：按目标函数声明的形参依次传入。
```erb
TRYCCALLFORM KOJO_{NO:TARGET}_GREETING
  ;函数存在时执行到这里后跳到 ENDCATCH 之后（RESULT 为其返回值）
CATCH
  PRINTL 展开后的函数名不存在，进入失败处理
ENDCATCH
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:350` → `new CALL_Instruction(true, false, true, true), EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，form=true, isJump=false, isTry=true, isTryCatch=true）

```text
构造（CALL_Instruction(form=true, isJump=false, isTry=true, isTryCatch=true)）:
    参数构造器 = SP_CALLFORM（FORM 格式字符串实参 + 自定义函数实参列表）
    flag = FLOW_CONTROL | FORCE_SETARG | IS_TRY | IS_TRYC | PARTIAL
    # 无 IS_JUMP：与 CALL 相同，调用完（或目标函数 RETURN 后）回到本命令的下一行顺序处

静态解析阶段 SetJumpTo():
    非常量（FORM 文本通常如此）→ useCallForm = true，运行期解析；
    恰为常量时：查找目标函数，找到则缓存 JumpTo / 调用深度 / 实参转换结果，
    找不到且为 TRY 系则静默放行（不记解析错误）。

运行期 DoInstruction():
    若实参是常量: 取静态缓存的 call / labelName / arg
    否则: labelName = FORM 文本求值展开
          call = CalledFunction.CallFunction(按名查找)
    若 call == null:
        # TRYCCALLFORM：isTry = true，不抛"函数未定义"
        若 func.JumpToEndCatch != null（本行作为 TRYC 块首行时为配对的 CATCH 行）:
            state.JumpTo(func.JumpToEndCatch)   # 失败 → 跳到 CATCH 行；
                                                # JumpTo 的落点是目标行的下一行，故从 CATCH 下一行开始执行失败处理
        返回
    call.IsJump = false                         # 与 CALL 相同：保留返回点
    若 arg == null: 按目标函数声明转换实参，失败 → 抛 CodeEE
    state.IntoFunction(call, arg, exm)          # 成功 → 进入目标函数，执行完返回
```

## 备注

- ecd 文档对 TRYCCALLFORM 仅一句话"与 TRYCALLFORM 对应的 TRYC 系版本"；调用/返回语义按 TRYCALL（即 CALLFORM 的 TRY 版）理解，示例为补写。
- 与其余 TRYC 系一致，失败跳转目标是解析期记录的 `JumpToEndCatch`（TRYC 块首行上 = 配对的 `CATCH` 行；`CATCH` 行自身的 `JumpToEndCatch` 才 = `ENDCATCH` 行，见 `Runtime/Script/Loader/ErbLoader.cs:1256`、`:1267`）。
- zh 文档未收录本命令。
