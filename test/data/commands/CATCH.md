# CATCH

- **类别**：命令
- **签名**：`CATCH`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系2 (TRYC-CATCH-ENDCATCH)」与「CATCH」小节；`Era-Chinese-Documentation` 未收录本命令

## 语义

`CATCH` 是 `TRYC` 系（TRYCCALL / TRYCJUMP / TRYCGOTO / TRYCCALLFORM / TRYCJUMPFORM / TRYCGOTOFORM）函数调用失败（目标函数或标签不存在）时转入的分支入口。语法上与 `IF`～`ELSE`～`ENDIF` 类似：`TRYC...` 成功时（函数存在）顺序执行到 `CATCH` 后，直接跳过 `CATCH`～`ENDCATCH` 之间的内容，继续执行 `ENDCATCH` 之后的行；失败时跳入 `CATCH` 分支执行。可以嵌套使用。若用 `GOTO` 等指令直接跳入 `TRYC`～`CATCH`～`ENDCATCH` 内部，会像 `IF` 系一样执行到 `CATCH` 或 `ENDCATCH` 处被拦截，跳到 `ENDCATCH` 的下一行继续。

## 用法

### `TRYC系调用` … `CATCH` … `ENDCATCH`
- `CATCH` 本身无参数，必须紧跟在 `TRYC` 系指令的函数体（可省略）之后。
- `CATCH`～`ENDCATCH` 之间写函数不存在时执行的处理。
```erb
TRYCCALL UNKNOWN_FUNC ;不存在的函数
	;函数存在时执行的处理（可省略直接到 CATCH）
CATCH
	;函数不存在时执行的处理
ENDCATCH
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:364` → `new CATCH_Instruction()`（flag = METHOD_SAFE | EXTENDED | FLOW_CONTROL | PARTIAL）；配对规则 `Runtime/Script/Statements/FunctionIdentifier.cs:472`（`funcMatch[CATCH] = "ENDCATCH"`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3413`（`CATCH_Instruction`）；配对解析在 `Runtime/Script/Loader/ErbLoader.cs:1242-1269`

```text
解析期（ErbLoader）:
    读到 CATCH 行:
        栈顶必须是 TRYC 系指令（TRYCGOTO/TRYCCALL/...），否则警告 "缺少对应的 TRYC"
        弹出 TRYC 行，令 TRYC.JumpToEndCatch = 本 CATCH 行（TRYC 失败时跳到 CATCH）
        把 CATCH 行压回嵌套栈
    读到 ENDCATCH 行:
        栈顶必须是 CATCH，否则警告；弹出 CATCH，令 CATCH.JumpToEndCatch = 本 ENDCATCH 行

运行期 CATCH_Instruction.DoInstruction(exm, func, state):
    // 顺序执行"流"到这里说明 TRYC 的目标存在、函数体已执行完
    // （或被 GOTO 跳入后一路执行到 CATCH）
    state.JumpTo(func.JumpToEndCatch)   // 无条件跳到配对的 ENDCATCH，跳过 CATCH 分支体
```

## 备注

- `ENDCATCH` 本身注册为 `ENDIF_Instruction`（`Runtime/Script/Statements/FunctionIdentifier.cs:365`，与 `ENDIF`、`ENDSELECT`、`DO`、`ENDFUNC` 同类），运行时是空操作，仅作解析配对锚点。
- `CATCH` 只能配 `TRYC` 系指令；配普通 `CALL`/`JUMP` 等会在解析期报错。
- ecd 文档与源码一致；zh 文档套件未收录该命令。
