# RETURNF

- **类别**：命令
- **签名**：RETURNF `<式>`
- **文档来源**：`ecd/docs/translation/Command.md`（RETURNF 小节）；Era-Chinese-Documentation 未收录该命令

## 语义

带有 `#FUNCTION`（整数函数）或 `#FUNCTIONS`（字符串函数）属性的用户自定义函数专用的返回指令。求值表达式并将其作为该函数的返回值。不支持多个返回值。在非 `#FUNCTION` 属性的普通函数中使用 `RETURNF` 会在解析时产生警告；在 `#FUNCTION` 中返回与函数类型不符的值（整数函数返回字符串或反之）也会产生警告。

## 用法

### RETURNF `<式>`
- `<式>`：任意表达式；若函数为 `#FUNCTION`（整数型），表达式的值作为函数值返回；`#FUNCTIONS`（字符串型）同理返回字符串。参数可以省略（返回 0 或空字符串）。

```erb
;调用示例
PRINTFORML 结果 = {MYFUNC(3)}    ;→ 结果 = 6

@MYFUNC(x)
#FUNCTION
RETURNF x * 2
```

```erb
@GREETING
#FUNCTIONS
RETURNF "你好，" + NAME:MASTER
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:259` → `new RETURNF_Instruction()`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:104`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3580`（`RETURNF_Instruction`）

```text
类 RETURNF_Instruction:
  构造: 参数构造器 = EXPRESSION_NULLABLE（单个任意类型表达式，可省略）;
        标志 = METHOD_SAFE | EXTENDED | FLOW_CONTROL

  SetJumpTo(...):    // 解析期静态检查
    label = func.ParentLabelLine
    if not label.IsMethod:                    // 所在函数不是 #FUNCTION/#FUNCTIONS
        ParserMediator.Warn("不能在此使用 RETURNF", func, 严重度 2)
    if func.Argument != null 且其 Term != null:
        if label.MethodType != term.GetOperandType():   // 返回值类型与函数类型不符
            if label.MethodType == typeof(long):
                Warn("整数函数中 RETURNF 返回了字符串")
            else if label.MethodType == typeof(string):
                Warn("字符串函数中 RETURNF 返回了整数")

  DoInstruction(exm, func, state):
    term = ((ExpressionArgument)func.Argument).Term
    ret = null
    if term != null:
        ret = term.GetValue(exm)     // 求值（整数或字符串取决于表达式类型）
    state.ReturnF(ret)               // 以表达式函数方式结束当前函数并返回该值
```

## 备注

- 与 `RETURN` 的关键区别：`RETURNF` 直接把值作为函数调用的取值结果（可在表达式中调用 `#FUNCTION`），不写入 `RESULT`。
- 参数可省略（EXPRESSION_NULLABLE），此时返回默认值；文档「不支持多个返回值」与源码单个参数构造一致。
- Era-Chinese-Documentation 未收录本命令（其 Custom_Expression.md 有 `#FUNCTION` 相关内容但未列此指令）。
