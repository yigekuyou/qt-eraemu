# HTML_TAGSPLIT

- **类别**：命令
- **签名**：`HTML_TAGSPLIT <字符串表达式>{, <数值变量>, <字符串变量>}`
- **文档来源**：`ecd/docs/translation/Command.md`「HTML系」`### HTML_TAGSPLIT` 小节；`Era-Chinese-Documentation/docs/HTML_PRINT.md`「### # HTML_TAGSPLIT」小节

## 语义

把目标字符串按 HTML 字符串解释，分割为"标签"与"平文"交替的片段：分割数写入 `RESULT`，各片段依次写入 `RESULTS:0`、`RESULTS:1`……。指定第 2、第 3 参数时，分割数与片段分别写入指定的数值变量、字符串变量（可带数组下标）而非 RESULT/RESULTS。分割处理发生错误时 `RESULT` 被赋为 -1。本命令**不验证**标签内容或配对是否合适；分割数超过目标字符串变量数组大小时，超出部分被丢弃。

## 用法

### HTML_TAGSPLIT <字符串表达式>
结果写入 RESULT/RESULTS。

### HTML_TAGSPLIT <字符串表达式>, <数值变量>, <字符串变量>
- 第 1 参：待分割的 HTML 字符串表达式。
- 第 2 参：接收分割数的数值变量。
- 第 3 参：接收各片段的字符串变量（按数组依次写入）。

```erb
HTML_TAGSPLIT "<p align='right'>A<! --comment-->B<font color='red'>C</font></p>"
;RESULTS:0 = "<p align='right'>"
;RESULTS:1 = "A"
;RESULTS:2 = "<! --comment-->"
;RESULTS:3 = "B"
;RESULTS:4 = "<font color='red'>"
;RESULTS:5 = "C"
;RESULTS:6 = "</font>"
;RESULTS:7 = "</p>"
;RESULT    = 8
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:387`（`new HTML_TAGSPLIT_Instruction()`，flag = EXTENDED | METHOD_SAFE；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:348`）
- 参数构建：`Runtime/Script/Statements/ArgumentBuilder.cs`（`FunctionArgType.SP_HTMLSPLIT`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:339`（`HTML_TAGSPLIT_Instruction`）；实际分割算法在 `HtmlManager.HtmlTagSplit(str)`

```text
DoInstruction(exm, func, state):
    spSplitArg ← (SpHtmlSplitArgument)func.Argument
    str ← spSplitArg.TargetStr.GetStrValue(exm)     # 求值第 1 参
    strs ← HtmlManager.HtmlTagSplit(str)            # 标签/平文交替分割

    若 strs == null:                                # 分割处理出错
        spSplitArg.Num.SetValue(-1, exm)            # 分割数变量（默认 RESULT）← -1
        return

    spSplitArg.Num.SetValue(strs.Length, exm)       # 分割数
    output ← (string[])spSplitArg.Var.GetArray()    # 目标字符串变量（默认 RESULTS）
    outputlength ← min(output.Length, strs.Length)
    Array.Copy(strs, output, outputlength)          # 超出目标数组长度的部分被丢弃
```

## 备注

- 两套文档（ecd 与 zh）对该命令的描述一致，zh 版还给出了与上方相同的 8 分割示例。
- 源码确认：错误时 `RESULT`（或指定变量）为 -1；超过数组大小时的截断行为与文档一致。
- "不验证标签内容/配对"：分割只做词法切分，语法错误只要切得开就照样返回，与文档一致。
- 其余无冲突。
