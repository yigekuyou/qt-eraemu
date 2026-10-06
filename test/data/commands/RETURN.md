# RETURN

- **类别**：命令
- **签名**：RETURN `<数值表达式>`(, `<数值表达式>`, `<数值表达式>`, ...)
- **文档来源**：`ecd/docs/translation/Command.md`（RETURN系 一节）；Era-Chinese-Documentation 未单独收录该命令

## 语义

从当前函数返回。这是 Eramaker 时代就存在的指令，但现在返回值可以是非常量的变量或表达式，并且支持多个返回值：指定多个返回值时会从前往后依次赋值给 `RESULT:0`、`RESULT:1`…… 若调用方以 `CALL` 调用，返回原调用处；以 `JUMP` 进入的函数同样结束当前调用帧。若函数执行结束而没有显式执行 `RETURN`，`RESULT` 的值为 0（视为隐式 `RETURN 0`）。

## 用法

### RETURN `<数值表达式>`(, `<数值表达式>`, ...)
- 每个 `<数值表达式>`：返回值，按顺序写入 `RESULT:0`、`RESULT:1`……
- 全部省略时等同于 `RETURN 0`。

```erb
A = 100
CALL TEST
PRINTFORMW RESULT == {RESULT}   ;→ RESULT == 1000

@TEST
RETURN A * 10
```

```erb
CALL MULTI
PRINTFORML {RESULT:0} {RESULT:1}   ;→ 10 20

@MULTI
RETURN 10, 20
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:257` → `new RETURN_Instruction()`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:102`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3383`（`RETURN_Instruction`）

```text
类 RETURN_Instruction:
  构造: 参数构造器 = INT_ANY（逗号分隔的任意整数表达式，可为空）; 标志 = FLOW_CONTROL

  DoInstruction(exm, func, state):
    expArrayArg = (ExpressionArrayArgument)func.Argument
    if expArrayArg.TermList.Length == 0:
        exm.VEvaluator.RESULT = 0     // 无参数时 RESULT = 0
        state.Return(0)               // 结束当前调用帧，返回值 0
        return
    termList = []
    foreach term in expArrayArg.TermList:
        termList.Add(term.GetIntValue(exm))     // 从左到右逐个求值
    exm.VEvaluator.SetResultX(termList)         // 依次写入 RESULT:0, RESULT:1, ...
    state.Return(exm.VEvaluator.RESULT)         // 弹出当前函数帧，回到调用处
```

## 备注

- 文档说「BEGIN 指令包含 RETURN 指令」（见 Era-Chinese-Documentation Flow.md：`BEGIN` 下面的语句从不执行，而 `LOADDATA`/`SAVEDATA` 与 `CALL` 一样返回原处），即 `BEGIN` 会隐式结束当前函数，等价于其后有 RETURN。
- `RETURNFORM` 是其接受 FORM 格式字符串的变体；`RETURNF` 是 `#FUNCTION`/`#FUNCTIONS` 专用变体，三者的行为差异见各自文档。
- Era-Chinese-Documentation 未单独收录本命令。
