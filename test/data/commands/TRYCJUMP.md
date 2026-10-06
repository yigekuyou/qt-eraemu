# TRYCJUMP

- **类别**：命令
- **签名**：
  - `TRYCJUMP <字符串> (, 参数1, 参数2……)`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系2 (TRYC-CATCH-ENDCATCH)」→「TRYCJUMP」；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

`TRYJUMP` 对应的 TRYC 系版本：与 `JUMP` 相同的函数跳转（可带参数），目标函数不存在时不报错；且失败时不是静默跳过，而是转入 `CATCH`～`ENDCATCH` 分支执行失败处理。

TRYC 系是与 `IF`～`ELSE`～`ENDIF` 类似的扩展语法：`TRYCJUMP` 之后到 `CATCH` 之前写成功路径的处理（可省略），`CATCH` 到 `ENDCATCH` 之间写失败处理，最后必须以 `ENDCATCH` 结束；可以嵌套。函数存在时执行到 `CATCH` 之前会直接跳到 `ENDCATCH` 的下一行继续。

## 用法

### `TRYCJUMP <字符串> (, 参数1, 参数2……)`
- `<字符串>`：目标函数名（不带 `@`），可为常量或字符串表达式。
- `参数1, 参数2……`：按目标函数声明的形参依次传入。
```erb
TRYCJUMP SHOW_EXTRA_SCENE, TARGET
  ;函数存在时会执行到这里，然后跳到 ENDCATCH 之后
CATCH
  PRINTL 函数不存在，进入失败处理
ENDCATCH
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:347` → `new CALL_Instruction(false, true, true, true), EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，form=false, isJump=true, isTry=true, isTryCatch=true）；配套 `CATCH_Instruction` 在同文件 `:3413`

```text
构造（CALL_Instruction(form=false, isJump=true, isTry=true, isTryCatch=true)）:
    参数构造器 = SP_CALL
    flag = FLOW_CONTROL | FORCE_SETARG | IS_JUMP | IS_TRY | IS_TRYC | PARTIAL
    # IS_TRYC | PARTIAL 使解析器把本行纳入 TRYC～CATCH～ENDCATCH 结构（本行即块首行），
    #   并在解析成功后填好 func.JumpToEndCatch = 配对的 CATCH 行

静态解析阶段 SetJumpTo():
    与 TRYJUMP 相同：常量函数名时查找目标函数并缓存 JumpTo / 实参转换；
    找不到且本指令为 TRY 系时不报解析错误。

运行期 DoInstruction():
    求得 call（常量取缓存，否则按名查找）
    若 call == null:
        # TRYCJUMP：isTry = true，不抛"函数未定义"
        若 func.JumpToEndCatch != null（本行必有，= 配对的 CATCH 行）:
            state.JumpTo(func.JumpToEndCatch)   # 失败 → 跳到 CATCH 行；JumpTo 的落点是目标行的下一行，
                                                # 故实际从 CATCH 的下一行开始执行失败处理分支
        返回
    call.IsJump = true                          # 与 JUMP 相同：顶替当前函数上下文
    若 arg == null: 转换实参，失败 → 抛 CodeEE
    state.IntoFunction(call, arg, exm)          # 成功 → 进入目标函数

（CATCH_Instruction.DoInstruction，:3413:
   顺序执行流入 CATCH 行时 state.JumpTo(func.JumpToEndCatch)，
   即成功路径越过 CATCH 段直达 ENDCATCH 之后。）
```

## 备注

- 文档称 TRYC 系"语法上与 IF～ELSE～ENDIF 类似（区别在于没有函数存在时的处理也可以）"；实现上 CATCH 的成功路径跳越由 `CATCH_Instruction` 完成，TRYC 命令本身只负责失败时跳往 `JumpToEndCatch`。
- 与 TRYJUMP 一样，本命令带 IS_JUMP 标志：目标函数的 RETURN 不会回到此处。
- zh 文档未收录本命令。
