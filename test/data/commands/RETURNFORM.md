# RETURNFORM

- **类别**：命令
- **签名**：RETURNFORM `<FORM格式文本>`(, `<FORM格式文本>`, `<FORM格式文本>`, ...)
- **文档来源**：`ecd/docs/translation/Command.md`（RETURN系 一节）；Era-Chinese-Documentation 未收录该命令

## 语义

`RETURN` 的变种。把参数指定的带 FORM 格式的字符串展开后作为数值表达式解析，然后执行 `RETURN`。同样支持多个返回值，从前往后依次赋值给 `RESULT:0`、`RESULT:1`…… 注意与 `RETURN` 不同，`%` 不会被当作取余运算符，而是被视为字符串表达式的开始。

## 用法

### RETURNFORM `<FORM格式文本>`(, `<FORM格式文本>`, ...)
- `<FORM格式文本>`：带 `{}`/`%@..%` 插值的格式字符串；展开结果整体作为整数表达式求值后写入 `RESULT`。

```erb
A = 100
CALL TEST
PRINTFORMW RESULT == {RESULT}    ;→ RESULT == 1000

@TEST
  STR = A * 10
  RETURNFORM %STR%
```

```erb
;OK。返回 A 的后两位。
RETURN A % 100

;出错。会把 % 之后当作字符串表达式来读。
RETURNFORM A % 100
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:258` → `new RETURNFORM_Instruction()`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:103`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3340`（`RETURNFORM_Instruction`）

```text
类 RETURNFORM_Instruction:
  构造: 参数构造器 = FORM_STR（单个 FORM 格式字符串参数）; 标志 = EXTENDED | FLOW_CONTROL

  DoInstruction(exm, func, state):
    aSt = CharStream( func.Argument.Term.GetStrValue(exm) )   // 先求出 FORM 展开后的字符串
    termList = []
    while not aSt.EOS:
        wc = LexicalAnalyzer.Analyse(aSt, LexEndWith.Comma)   // 以逗号为界逐段做词法分析
        termList.Add( ExpressionParser.ReduceIntegerTerm(wc).GetIntValue(exm) )
        aSt.ShiftNext()          // 跳过逗号
        LexicalAnalyzer.SkipHalfSpace(aSt)
    if termList.Count == 0:
        termList.Add(0)          // 展开结果为空时返回 0
    exm.VEvaluator.SetResultX(termList)   // 依次写入 RESULT:0, RESULT:1, ...
    state.Return(exm.VEvaluator.RESULT)   // 结束当前函数，回到调用处
```

## 备注

- 与文档一致：多个返回值不是靠多个参数（参数只有一个 FORM 字符串），而是在展开后的字符串里用逗号分隔的多个整数表达式，实现按逗号逐段解析。
- 展开结果为空字符串时按返回 0 处理，文档未提及。
- 文档中「`RETURNFORM A % 100` 出错」的例子与源码吻合：`%` 先被 FORM 解析为字符串表达式边界。
- Era-Chinese-Documentation 未收录本命令。
