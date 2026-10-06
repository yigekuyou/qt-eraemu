# PRINTPLAIN

- **类别**：命令
- **签名**：PRINTPLAIN <字符串表达式>
- **文档来源**：`ecd/docs/translation/Command.md`「### PRINTPLAIN (|FORM）」；Era-Chinese-Documentation 未收录本命令

## 语义

把字符串作为**纯文本**输出：不会像 `PRINTBUTTON` 那样被解析/转化成按钮，文本中的 `[` `]` 等按钮记号按普通字符显示。参数是普通字符串表达式（可用引号或变量）。输出后不换行；追加到当前输出缓冲的末尾。受 `SKIPDISP` 影响。

## 用法

### PRINTPLAIN <字符串表达式>

- `<字符串表达式>`：STR_NULLABLE 参数，可省略（省略时不输出任何内容）。
- 输出为纯文本，不生成按钮、不解释 HTML/按钮记号。
- 不换行、不等待输入。

```erb
PRINTPLAIN [0] 这里的方括号只是普通文字
PRINTL
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:177`（`addFunction(FunctionCode.PRINTPLAIN, argb[STR_NULLABLE], METHOD_SAFE | EXTENDED)`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:135`
- 实现：`Runtime/Script/Process.ScriptProc.cs:145-153`（switch-case，直接调用控制台）；`UI/Game/EmueraConsole.Print.cs:621`（`PrintPlain`）

```text
执行（Process.ScriptProc.cs，PRINTPLAIN / PRINTPLAINFORM 共用分支）：
    若 skipPrint（SKIPDISP 1 且不在 NOSKIP 区间）→ break
    Console.UseUserStyle = true
    Console.UseSetColorStyle = true          ← 应用 SETCOLOR 颜色
    term = ((ExpressionArgument)func.Argument).Term
    str  = term.GetStrValue(exm)             ← 求值字符串表达式（参数本身不是 FORM 文本）
    Console.PrintPlain(str)：
        若 str 为空 → 不做任何事
        printBuffer.AppendPlainText(str, Style)   ← 以「纯文本」节点追加到输出缓冲
        （不经过按钮/HTML 解析路径，与 Print/PrintButton 不同）
    break
```

## 备注

- ecd 文档该节只有一句话（输出纯文本、不会转化成按钮），没有示例；示例为本文件按语义补写。
- 与 `PRINTPLAINFORM` 的唯一差别在参数构造器：本命令是 `STR_NULLABLE`（普通字符串表达式），后者是 `FORM_STR_NULLABLE`（FORM 格式文本）；两者的执行分支完全相同。
