# TRYCGOTO

- **类别**：命令
- **签名**：
  - `TRYCGOTO <字符串>`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系2 (TRYC-CATCH-ENDCATCH)」→「TRYCGOTO」；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

`TRYGOTO` 对应的 TRYC 系版本：与 `GOTO` 相同的 `$` 标签跳转，标签不存在时不报错，且失败时转入 `CATCH`～`ENDCATCH` 分支执行失败处理。

TRYC 系结构：命令之后到 `CATCH` 之前为成功路径（可省略），`CATCH` 到 `ENDCATCH` 之间为失败处理，可嵌套；标签存在时（即跳转成功后）执行到 `CATCH` 之前会跳到 `ENDCATCH` 的下一行。用 GOTO 系指令直接跳入 `TRYC`～`CATCH`～`ENDCATCH` 内时，与 `IF` 结构一样执行到 `CATCH`、`ENDCATCH` 之前后跳到 `ENDCATCH` 的下一行继续。

## 用法

### `TRYCGOTO <字符串>`
- `<字符串>`：目标 `$` 标签名，可为常量或字符串表达式。
```erb
TRYCGOTO SKIP_PART
  ;标签存在时执行到这里后跳到 ENDCATCH 之后
CATCH
  PRINTL 标签不存在，进入失败处理
ENDCATCH
$SKIP_PART
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:360` → `new GOTO_Instruction(false, true, true), EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3746`（`GOTO_Instruction`，form=false, isTry=true, isTryCatch=true）

```text
构造（GOTO_Instruction(form=false, isTry=true, isTryCatch=true)）:
    参数构造器 = SP_CALL（字符串实参）
    flag = METHOD_SAFE | FLOW_CONTROL | FORCE_SETARG | IS_TRY | IS_TRYC | PARTIAL
    # IS_TRYC | PARTIAL 使解析器把本行（TRYC 块首行）纳入 TRYC～CATCH～ENDCATCH 结构，
    # 并填好 func.JumpToEndCatch = 配对的 CATCH 行

静态解析阶段 SetJumpTo():
    常量标签名时：GetLabelDollar 查找本函数内的标签；
        找不到且为 TRY 系 → 静默返回（func.JumpTo 保持 null）；
        找到但 IsError → 警告；找到 → func.JumpTo = jumpto。

运行期 DoInstruction():
    若实参是常量:
        若 func.JumpTo != null → jumpto = func.JumpTo
        否则 → 返回（标签不存在）
    否则:
        label = 字符串表达式求值
        jumpto = state.CurrentCalled.CallLabel(按名查找)
    若 jumpto == null:
        # TRYCGOTO：isTry = true，不抛"标签未定义"
        若 func.JumpToEndCatch != null（本行即 TRYC 块首行，= 配对的 CATCH 行）:
            state.JumpTo(func.JumpToEndCatch)   # 失败 → 跳到 CATCH 行；JumpTo 的落点是目标行的下一行，
                                                # 故实际从 CATCH 的下一行开始执行失败处理分支
        返回
    否则若 jumpto.IsError → 抛 CodeEE("非法标签名")
    否则 state.JumpTo(jumpto)                   # 成功 → 跳到标签处
```

## 备注

- 与 TRYGOTO 相比仅多出 `IS_TRYC | PARTIAL` 标志与 `JumpToEndCatch` 失败跳转；跳转成功时成功路径中 `CATCH` 段由 `CATCH_Instruction`（`Runtime/Script/Statements/Instraction.Child.cs:3413`）跳过。
- 文档与源码一致：本命令对"非法标签名"（IsError）仍抛 CodeEE，只有"标签不存在"才走 TRY/CATCH 逻辑。
- zh 文档未收录本命令。
