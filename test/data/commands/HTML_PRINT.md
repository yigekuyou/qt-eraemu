# HTML_PRINT

- **类别**：命令
- **签名**：`HTML_PRINT <字符串表达式>`（本仓库 EM 扩展另支持可选第 2 参数 `HTML_PRINT <字符串表达式>{, <数值表达式>}`）
- **文档来源**：`ecd/docs/translation/Command.md`「HTML系」`### HTML_PRINT` 小节；`Era-Chinese-Documentation/docs/HTML_PRINT.md`「## # HTML_PRINT」小节

## 语义

利用类似 HTML 的标签进行 PRINT 输出的指令。参数不是 PRINT 那样的字符串常量，而是与 `PRINTS` 相同的字符串表达式，并且会自动换行，因此实际动作更接近 `PRINTSL`。可用 `<tagname attribute='属性值'>文本</tagname>` 的形式（属性值建议用单引号括起，以区别于 Emuera 自身的字符串）。`HTML_PRINT` 的绘制**不受** `ALIGNMENT`、`SETFONT`、`COLOR`、`FONTSTYLE` 等指令影响，这些效果必须全部用 HTML 标签指定。跳过显示（SKIPDISP）生效时不输出。标签语法详情参阅 HTML_PRINT 相关文档（p、font、b、i、s、u、nobr、br、button 等标签及工具提示）。

## 用法

### HTML_PRINT <字符串表达式>
- `<字符串表达式>`：包含 HTML 标签的字符串表达式（与 PRINTS 相同的表达式规则，可用 FORM 格式化）。

```erb
HTML_PRINT "<p align='right'><font color='red'>红色右对齐文本</font></p>"
HTML_PRINT @"<b>{A} 点</b>"
;执行后自动换行，相当于 PRINTSL
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:386`（`new HTML_PRINT_Instruction()`，flag = EXTENDED | METHOD_SAFE；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:347`）
- 参数构建：`Runtime/Script/Statements/ArgumentBuilder.cs:355`（`SP_HTML_PRINT_ArgumentBuilder`，1~2 个参数：第 1 参字符串表达式必填，第 2 参数值表达式可选）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:312`（`HTML_PRINT_Instruction`）；最终绘制 `UI/Game/EmueraConsole.Print.cs:498`（`PrintHtml(string, bool toPrintBuffer)`）

```text
DoInstruction(exm, func, state):
    若 GlobalStatic.Process.SkipPrint:      # 处于跳过显示状态
        return                              # 什么都不输出

    arg ← (SpHtmlPrint)func.Argument
    若 arg 是常量字符串:
        exm.Console.PrintHtml(arg.ConstStr, arg.ConstInt != 0)
    否则:
        toBuffer ← (arg.Opt == null) ? false : (arg.Opt 求值 != 0)
        exm.Console.PrintHtml(arg.Str.GetStrValue(exm), toBuffer)

# PrintHtml(str, toPrintBuffer):
#   用 HtmlManager 解析标签并绘制到控制台；
#   toPrintBuffer 为 true 时（EM 私家版扩展，第 2 参非 0）输出进 PRINT 缓冲
#   而不是立即绘制。
```

## 备注

- **本仓库扩展与文档的差异**：ecd 与 zh 文档均只描述单参数形式；本仓库源码（EM 私家版 HTML_PRINT 扩展，注册处带 `#region EM_私家版_HTML_PRINT拡張`）额外支持可选第 2 个数值参数，非 0 时把结果送入打印缓冲（`PrintHtml` 的 `toPrintBuffer`）。文档未收录该扩展。
- 两套文档对"不受 ALIGNMENT/SETFONT/COLOR/FONTSTYLE 影响、自动换行、类似 PRINTSL"的描述完全一致。
- 跳过显示（`SkipPrint`）时不绘制——两套文档均未提及。
- 其余无冲突。
