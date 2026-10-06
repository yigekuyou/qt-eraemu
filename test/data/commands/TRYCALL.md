# TRYCALL

- **类别**：命令（Eramaker 无）
- **签名**：TRYCALL `<函数名>` (, 参数1, 参数2……)
- **文档来源**：`ecd/docs/translation/Command.md`「### TRYCALL」（另见「### TRYJUMP」小节末尾对 TRY 系的说明）；Era-Chinese-Documentation 无对应小节

## 语义

与 `CALL` 相同的自制函数调用指令，但指定的函数不存在时不会出错，而是什么都不做，直接继续执行下一行。可以像 `CALL` 一样传参数（参数按被调函数 `#DIM`/`#FUNCTION` 声明匹配；函数名后也可用 `,函数名@函数名` 形式指定子函数链，与 CALL 相同）。

与 `TRYCCALL` 不同，`TRYCALL` 不带 `CATCH`～`ENDCATCH` 结构；函数不存在时既不报错也没有专门的处理块。函数存在时行为与 `CALL` 完全一致：调用后返回调用处继续执行。

## 用法

### TRYCALL `<函数名>` (, 参数1, 参数2……)
- 第 1 参数：要调用的函数名，可以是常量字符串或字符串表达式（用表达式时编译期无法解析，运行期按名字查找，找不到则静默跳过）。
- 之后的参数：传给被调函数的实参，按被调函数的参数声明匹配，个数或类型不符会抛错（这与函数不存在时的静默处理不同）。

```erb
;@SHOW_DATA 等函数存在则调用，不存在则什么都不发生
TRYCALL SHOW_DATA
TRYCALL UNKNOWN_FUNC, 1, 2   ;不存在 → 静默跳过
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:342`（`new CALL_Instruction(false, false, true, false), EXTENDED`；即 form=false, isJump=false, isTry=true, isTryCatch=false，flag = FLOW_CONTROL | FORCE_SETARG | IS_TRY）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3623`（`CALL_Instruction`，与 CALL/JUMP/TRYC 系共用一个类）；参数构造用 `FunctionArgType.SP_CALL`（函数名 + 参数列表，存入 `SpCallArgment`，见 `Runtime/Script/Statements/Argument.cs:272`）

```text
编译期 SetJumpTo:
  若第 1 参数不是常量: useCallForm = true，运行期再解析
  否则: labelName = 常量函数名
        call = CalledFunction.CallFunction(Process, labelName, 本行)
        若 call == null 且 本指令是 TRY 系:
            不设置 FunctionoNotFoundName（即不报“函数未定义”错）
        若 call != null:
            预解析实参（ConvertArg），失败则发警告
            func.JumpTo = 函数入口行

运行期 DoInstruction:
  若参数是常量: 直接取编译期解析好的 call / labelName / 实参
  否则: labelName = 函数名表达式求值
        call = 按名查找函数
  若 call == null:
      若非 TRY 系: 抛 CodeEE("函数 xxx 未定义")
      本指令 isTry = true:
          若 func.JumpToEndCatch != null（TRYC 系块首行才有）: 跳到配对的 CATCH 行
              （JumpTo 的落点是目标行的下一行，故实际从 CATCH 的下一行开始执行失败处理）
          否则（TRYCALL 恒为 null）: 什么都不做，返回，继续下一行
  call.IsJump = false          // CALL 语义：调用后返回
  若编译期没解析好实参: call.ConvertArg(原始参数)，失败抛 CodeEE
  state.IntoFunction(call, arg, exm)   // 进入函数，RETURN 后回到本行之后
```

## 备注

- ecd 文档的签名写 `TRYCALL <字符串>`，源码实现同样接受表达式（非常量名走运行期查找），两者一致。
- 「什么都不做」在源码中体现为：`call == null` 且 `isTry` 且 `func.JumpToEndCatch == null` 时直接 `return`（`JumpToEndCatch` 只会被解析器写到 TRYC 系块首行与 `CATCH` 行上）。
- 与 `TRYCCALL`（本批另一条）的唯一区别是构造参数 `isTryCatch`：false 时没有 CATCH 块可用（本行的 `JumpToEndCatch` 恒为 null）；true 时解析器会把本行（TRYC 块首行）的失败跳转目标 `JumpToEndCatch` 设为配对的 `CATCH` 行，失败时从 `CATCH` 的下一行进入失败处理。
