# TRYCCALL

- **类别**：命令（Eramaker 无）
- **签名**：TRYCCALL `<函数名>` (, 参数1, 参数2……)
- **文档来源**：`ecd/docs/translation/Command.md`「### TRYCCALL」（另见「### CALL·JUMP·GOTO系2 (TRYC-CATCH-ENDCATCH)」小节）；Era-Chinese-Documentation 无对应小节

## 语义

与 `TRYCALL` 对应的 `TRYC` 系版本：调用指定函数，函数不存在时不出错，而是转入 `CATCH`～`ENDCATCH` 块执行失败时的处理。语法上与 `IF`～`ELSEIF`～`ELSE`～`ENDIF` 类似，可以嵌套使用：

```erb
TRYCCALL UNKNOWN_FUNC   ;不存在的函数
	;函数存在时执行的处理（可省略，直接到 CATCH）
CATCH
	;函数不存在时执行的处理
ENDCATCH
```

函数存在时，其调用及之后的语句正常执行到 `CATCH` 之前，然后跳过 `CATCH` 块到 `ENDCATCH` 之后继续。用 `GOTO` 等指令直接跳入 `TRYC` 系～`CATCH`～`ENDCATCH` 内部时，与跳入 `IF` 块内部一样，会执行到 `CATCH`/`ENDCATCH` 之前，然后跳到 `ENDCATCH` 的下一行继续。除失败处理外，参数传递与 `CALL` 相同。

## 用法

### TRYCCALL `<函数名>` (, 参数1, 参数2……) [处理] CATCH [失败处理] ENDCATCH
- 第 1 参数：函数名，常量字符串或字符串表达式。
- 之后：传给被调函数的实参。
- `CATCH`～`ENDCATCH`：函数不存在时执行的块；`CATCH` 之前、函数存在时要执行的语句可省略。

```erb
TRYCCALL SHOW_DETAIL, 3
	PRINTL 函数存在，调用完成
CATCH
	PRINTL SHOW_DETAIL 不存在
ENDCATCH
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:348`（`new CALL_Instruction(false, false, true, true), EXTENDED`；即 form=false, isJump=false, isTry=true, isTryCatch=true，flag = FLOW_CONTROL | FORCE_SETARG | IS_TRY | IS_TRYC | PARTIAL）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，与 CALL/TRYCALL 系共用）；`CATCH`/`ENDCATCH` 分别注册于 `Runtime/Script/Statements/FunctionIdentifier.cs:364`（`CATCH_Instruction`）与 `:365`（`ENDIF_Instruction`）；`func.JumpToEndCatch` 由解析器在配对 `CATCH`/`ENDCATCH` 时设置（TRYC 系行上它指向 `CATCH` 行，`CATCH` 行上它指向 `ENDCATCH` 行，见 `Runtime/Script/Loader/ErbLoader.cs:1256`、`:1267`）

```text
与 TRYCALL 的实现完全相同（CALL_Instruction），仅 isTryCatch = true：
  解析期：
      指令带 IS_TRYC | PARTIAL 标志 → 解析器为其寻找配对的 CATCH/ENDCATCH，
      并把 func.JumpToEndCatch 指向配对的 CATCH 行（失败时进入 CATCH 分支体）。
  运行期 DoInstruction:
      解析函数名，按名查找 call
      若 call == null:
          isTry = true → 不抛 CodeEE
          若 func.JumpToEndCatch != null（TRYCCALL 必有）:
              state.JumpTo(func.JumpToEndCatch)   // 跳到 CATCH 行，落点为 CATCH 的下一行＝失败处理块开头
          返回
      call.IsJump = false
      实参转换失败 → 抛 CodeEE
      state.IntoFunction(call, arg, exm)          // 调用后返回，继续到 CATCH 之前
      （顺序执行到 CATCH 行时，由 CATCH_Instruction 跳到 ENDCATCH，从而跳过 CATCH 分支体）
```

## 备注

- ecd 文档明确 TRYCCALL 与 TRYCALL 的区别仅在「可由 `CATCH`～`ENDCATCH` 捕获」，与源码构造参数差异（`isTryCatch` false→true）一致。
- `TRYCCALL` 之后、`CATCH` 之前的语句仅在函数存在时执行；若直接省略这些语句，`TRYCCALL` 可紧接 `CATCH`。
- 同系列还有 TRYCJUMP / TRYCCALLFORM / TRYCGOTO / TRYCJUMPFORM / TRYCGOTOFORM，均共用 `CALL_Instruction` 或 `GOTO_Instruction`，仅构造参数不同。
