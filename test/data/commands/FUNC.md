# FUNC

- **类别**：命令（EE 扩展语法，列表跳转/调用的候选项）
- **签名**：`FUNC <字符串>`（TRYGOTOLIST 形式下仅标签名，不带参数、不带子名）
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系」`### FUNC` 小节（另有 `### TRYGOTOLIST`、`### ENDFUNC` 关联小节）；`Era-Chinese-Documentation`（zh 套件）未收录该命令

## 语义

`FUNC` 不能单独使用，只能写在 `TRYCALLLIST`／`TRYJUMPLIST`／`TRYGOTOLIST` 与 `ENDFUNC` 之间，作为候补函数（标签）列表的一项。运行时引擎按 `FUNC` 的书写顺序依次尝试目标：以 `TRYCALLLIST`/`TRYJUMPLIST` 使用时按普通函数名格式调用（可带参数），以 `TRYGOTOLIST` 使用时按 `$` 标签跳转（此时 FUNC 的目标只能是不带参数、不带子名的标签名）；第一个存在（可成功解析）的目标被执行，之后转到 `ENDFUNC`；全部失败则直接跳到 `ENDFUNC`。列表内除 `FUNC` 和 `ENDFUNC` 外不得书写其他语句。它等价于嵌套的 `TRYCCALL`～`CATCH` 链。

## 用法

### FUNC <字符串> —— 在 TRYCALLLIST / TRYJUMPLIST 中
- `<字符串>`：候补函数名。可带参数（作为调用实参）。

```erb
TRYCALLLIST
  FUNC 函数1
  FUNC 函数2
ENDFUNC
;依次尝试调用 函数1、函数2，先找到谁就调用谁；都不存在则跳到 ENDFUNC
```

### FUNC <字符串> —— 在 TRYGOTOLIST 中
- `<字符串>`：候补 `$` 标签名。不得带子名（`@XX:1` 形式）或参数，否则加载期告警且该项无效。

```erb
TRYGOTOLIST
  FUNC $LABEL_A
  FUNC $LABEL_B
ENDFUNC
;跳到当前函数内第一个存在的标签
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:369`（`argb[FunctionArgType.SP_CALLFORM]`，flag = EXTENDED | FLOW_CONTROL | PARTIAL | FORCE_SETARG；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:165`；`ENDFUNC` 注册为 `new ENDIF_Instruction()`，:370）
- 加载期处理：`Runtime/Script/Loader/ErbLoader.cs:1396`（`case FunctionCode.FUNC`）与 :1000（列表内容校验）
- 运行期：`FUNC` 自身无执行体；由所在列表指令执行——`Runtime/Script/Process.ScriptProc.cs:831`（TRYCALLLIST/TRYJUMPLIST 分支）、:859（TRYGOTOLIST 分支）

```text
# 加载期（ErbLoader）
case FUNC:
    pFunc ← nestStack 栈顶
    若 pFunc 为空 或 pFunc 不是 TRYCALLLIST/TRYJUMPLIST/TRYGOTOLIST:
        告警 "FUNC 不在列表语法内"，本行作废
    若 func.Argument == null:
        告警 "TRYCALLLIST 中出现非法指令"，本行作废
    若 pFunc 是 TRYGOTOLIST:
        若 FUNC 带子名（SubNames 非空）: 告警 "TRYGOTOLIST 的 FUNC 不能带子名"
        若 FUNC 带参数（RowArgs 非空）: 告警 "TRYGOTOLIST 的 FUNC 目标不能带参数"
    pFunc.callList.Add(func)      # 挂到列表指令的候补链上

# 运行期（由 TRYCALLLIST / TRYJUMPLIST 驱动，Process.ScriptProc.cs:831）
对 func.callList 中每个 FUNC 行 iLine 依次:
    funcName ← 求值 iLine.Argument.FuncnameTerm
    callto ← CalledFunction.CallFunction(this, funcName, func.JumpTo)
    若 callto == null: continue          # 该候选不存在，试下一个
    callto.IsJump ← 列表是否为 TRYJUMPLIST
    args ← callto.ConvertArg(cfa.RowArgs)   # 转换 FUNC 行上写的参数
    若转换失败: throw CodeEE
    state.IntoFunction(callto, args)        # 进入目标函数
    return
全部失败: state.JumpTo(func.JumpTo)      # func.JumpTo 即 ENDFUNC 行

# 运行期（由 TRYGOTOLIST 驱动，Process.ScriptProc.cs:859）
对 func.callList 中每个 FUNC 行依次:
    funcName ← 求值其标签名
    jumpto ← state.CurrentCalled.CallLabel(this, funcName)
    若 jumpto != null: break              # 第一个存在的标签
若 jumpto == null: state.JumpTo(func.JumpTo)   # 跳到 ENDFUNC
否则: state.JumpTo(jumpto)
```

## 备注

- ecd 文档写 "`TRYLIST` 系～`ENDFUNC` 内不能书写上述语法以外的内容"，实际允许的容器是 `TRYCALLLIST`、`TRYJUMPLIST`、`TRYGOTOLIST` 三种（源码 `Runtime/Script/Loader/ErbLoader.cs:1000` 的校验；"TRYLIST" 应为文档笔误）。
- ecd 文档的 `### ENDFUNC` 小节说"结束由 TRYCALLLIST、TRYJUMPLIST、TRYGOTOLIST 开始的列表调用"，与源码一致（ENDFUNC 行被解析为容器的 JumpTo 目标）。
- FUNC 行本身永远不会被顺序执行到（列表指令必然跳走），在非 DEBUG 构建下若通过 GOTO 等异常途径落到 FUNC 行，它没有执行体、等于空操作。
- 其余无冲突。
