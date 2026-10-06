# TRYCALLLIST

- **类别**：命令（Eramaker 无）
- **签名**：TRYCALLLIST（无参数，后接若干 `FUNC` 行，以 `ENDFUNC` 结束）
- **文档来源**：`ecd/docs/translation/Command.md`「### TRYCALLLIST」「### FUNC」「### ENDFUNC」；Era-Chinese-Documentation 无对应小节

## 语义

与 `FUNC`～`ENDFUNC` 配合，按顺序尝试调用多个函数中的第一个存在的函数。语法：

```erb
TRYCALLLIST
  FUNC 函数1
  FUNC 函数2
ENDFUNC
```

从上到下依次尝试调用各 `FUNC` 指定的函数：某个函数存在则调用它（参数照常传递），调用结束后跳到 `ENDFUNC` 继续执行；不存在则移到下一行 `FUNC`（或 `ENDFUNC`）。全部失败时直接落到 `ENDFUNC` 之后继续。它与下面的脚本等价：

```erb
TRYCCALL 函数1
CATCH
  TRYCCALL 函数2
  CATCH
  ENDCATCH
ENDCATCH
```

限制：`TRYCALLLIST`～`ENDFUNC` 之间只能写 `FUNC` 行，不能书写其他语句；列表不能嵌套（嵌套时编译警告）。相应地还有 `TRYJUMPLIST`（跳转不返回）与 `TRYGOTOLIST`（$ 标签跳转）两个变体。

## 用法

### TRYCALLLIST … FUNC `<函数名>` (, 参数…) … ENDFUNC
- `TRYCALLLIST` 自身不带参数，仅作为列表开始。
- 每个 `FUNC` 行给出候选函数名（FORM 格式）及可选实参。
- `ENDFUNC` 结束列表。

```erb
TRYCALLLIST
	FUNC EVENT_FIRST_1
	FUNC EVENT_FIRST
ENDFUNC
PRINTL 全部候选都不存在时也会执行到这里
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:366`（`argb[FunctionArgType.VOID], EXTENDED | FLOW_CONTROL | PARTIAL | IS_TRY`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:830`（`case FunctionCode.TRYCALLLIST:`，与 `TRYJUMPLIST` 共用一个 case）；列表结构由 `Runtime/Script/Loader/ErbLoader.cs:1387`（TRYCALLLIST 分支）与 `:1406`（FUNC 分支）、`:1423`（ENDFUNC 分支）在解析期构建
- 辅助：`Runtime/Script/Statements/LogicalLine.cs:173`（`List<InstructionLine> callList`）

```text
解析期（ErbLoader）:
  遇 TRYCALLLIST:
      若嵌套栈中已有 TRY*LIST → 警告“不能嵌套”
      func.callList = 空列表；入栈
  遇 FUNC:
      栈顶必须是 TRYCALLLIST/TRYJUMPLIST/TRYGOTOLIST，否则警告
      把 FUNC 行加入栈顶的 callList
  遇 ENDFUNC:
      校验栈顶，func.JumpTo = ENDFUNC 行；出栈

运行期（ScriptProc.DoScript）:
  case TRYCALLLIST / TRYJUMPLIST:
      foreach (iLine in func.callList):        // 依序处理每个 FUNC 行
          cfa = (SpCallArgment)iLine.Argument
          funcName = cfa.FuncnameTerm.GetStrValue(exm)   // FORM 求值
          callto = CalledFunction.CallFunction(按名查找)
          若 callto == null: continue           // 不存在 → 尝试下一个 FUNC
          callto.IsJump = 本指令是否为 JUMP 系  // TRYCALLLIST 恒为 false（调用）
          args = callto.ConvertArg(cfa.RowArgs)  // 实参不匹配 → 抛 CodeEE
          state.IntoFunction(callto, args, exm)  // 进入函数
          return true                            // 调用成功，返回后从 ENDFUNC 后继续
      // 所有 FUNC 都失败：
      state.JumpTo(func.JumpTo)                  // JumpTo 即 ENDFUNC 行
```

## 备注

- ecd 文档说 TRYCALLLIST 与 FUNC～ENDFUNC 配合；源码中 FUNC/ENDFUNC 同时服务于 TRYCALLLIST/TRYJUMPLIST/TRYGOTOLIST 三种列表。
- 与逐个 `TRYCCALL`～`CATCH` 嵌套等价（文档自己也给出了等价脚本）；区别仅是书写更简洁。
- `FUNC` 行的实参不匹配（函数存在但参数对不上）会抛错而不是继续尝试下一个 FUNC——「静默失败」只适用于「函数不存在」。
