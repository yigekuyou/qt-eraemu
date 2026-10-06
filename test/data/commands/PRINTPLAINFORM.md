# PRINTPLAINFORM

- **类别**：命令
- **签名**：PRINTPLAINFORM <FORM格式文本>
- **文档来源**：`ecd/docs/translation/Command.md`「### PRINTPLAIN (|FORM）」；Era-Chinese-Documentation 未收录本命令

## 语义

`PRINTPLAIN` 的 FORM 版本：先把参数当作 FORM 格式文本求值（展开 `{数值表达式}`、`%字符串表达式%` 等），再把结果作为**纯文本**输出——不会转化成按钮，`[` `]` 等记号按普通字符显示。输出后不换行。受 `SKIPDISP` 影响。

## 用法

### PRINTPLAINFORM <FORM格式文本>

- `<FORM格式文本>`：FORM_STR_NULLABLE 参数，可省略（省略时不输出任何内容）。
- 展开后输出为纯文本，不生成按钮。
- 不换行、不等待输入。

```erb
A = 3
PRINTPLAINFORM 第{A}项 [0] 不是按钮
PRINTL
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:178`（`addFunction(FunctionCode.PRINTPLAINFORM, argb[FORM_STR_NULLABLE], METHOD_SAFE | EXTENDED)`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:136`
- 实现：`Runtime/Script/Process.ScriptProc.cs:145-153`（switch-case，与 PRINTPLAIN 共用）；`UI/Game/EmueraConsole.Print.cs:621`（`PrintPlain`）

```text
执行（Process.ScriptProc.cs，PRINTPLAIN / PRINTPLAINFORM 共用分支）：
    若 skipPrint → break
    Console.UseUserStyle = true
    Console.UseSetColorStyle = true          ← 应用 SETCOLOR 颜色
    term = ((ExpressionArgument)func.Argument).Term
    str  = term.GetStrValue(exm)
        （FORM_STR_NULLABLE 的 Term 是 StrForm 求值树，此处完成 {...} / %...% 展开）
    Console.PrintPlain(str)：
        若 str 为空 → 不做任何事
        printBuffer.AppendPlainText(str, Style)   ← 纯文本节点，不经按钮/HTML 解析
    break
```

## 备注

- ecd 文档把 `PRINTPLAIN (|FORM)` 写在同一个小节，只说明「输出纯文本、不会转化成按钮」，未给出 FORM 版本的示例；示例为本文件按语义补写。
- 与 `PRINTPLAIN` 的差别仅在参数构造器（`FORM_STR_NULLABLE` vs `STR_NULLABLE`），运行时走同一分支。
