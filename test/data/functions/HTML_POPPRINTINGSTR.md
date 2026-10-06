# HTML_POPPRINTINGSTR

- **类别**：式中函数（HTML 系函数）
- **签名**：str HTML_POPPRINTINGSTR()
- **文档来源**：zh 套件 `zh/HTML_PRINT.md`「相关函数」→「### # str HTML_POPPRINTINGSTR()」；`ecd/Command.md` 的 HTML 系章节未收录；`ecd/Expression.md` 未收录

## 语义

取出当前正待打印（PRINT 缓冲区中尚未换行输出）的内容，作为 HTML 格式的字符串返回，并清空该缓冲区（"pop"）。返回的字符串是 HTML 格式：行内片段以 `<br>`、按钮以 button 标签等形式还原，文本经 HTML 转义。

因为返回结果不附带 `<p>` 标签（needPandN = false），`ALIGNMENT` 指令设置的对齐方式不会反映在其中——这是它和 `HTML_GETPRINTEDSTR` 的一个明显区别。

本函数无参数；缓冲区为空时返回空字符串。副作用是清空 PRINT 缓冲区，等效于把待打印内容「拿走」而不显示。

## 用法

### str HTML_POPPRINTINGSTR()
- 无参数。
- 返回值：待打印缓冲区内容的 HTML 格式字符串（不含 p 标签），随后缓冲区被清空。
```erb
PRINTS "A", "B"          ; 进入缓冲区但尚未换行
S = HTML_POPPRINTINGSTR()  ; 取走并清空缓冲区
PRINTL C                 ; 此时控制台上是 "C"（AB 未被显示）
HTML_PRINT S             ; 再把取出的内容以 HTML 画出来
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:144`（`["HTML_POPPRINTINGSTR"] = new HtmlPopPrintingStrMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5109`（`HtmlPopPrintingStrMethod`）；核心转换在 `UI/Game/HtmlManager.cs:308`（`HtmlManager.DisplayLine2Html`）

```text
HtmlPopPrintingStrMethod:
构造：返回类型 = string；参数 = []（无参数）；CanRestructure = false。

GetStrValue(exm, args):
    dispLines = exm.Console.PopDisplayingLines()
        ; 取出 PRINT 等待换行中的缓冲行并清空该缓冲区（pop 语义）
    若 dispLines == null:
        返回 ""
    返回 HtmlManager.DisplayLine2Html(dispLines, false)
        ; needPandN = false：不附加 <p align=...> 与 <nobr>，
        ; 因此对齐方式不被反映

HtmlManager.DisplayLine2Html(lines, needPandN) 概要:
    （与 HTML_GETPRINTEDSTR 共用，needPandN = false 时跳过
      p/nobr 包裹；逐行以 <br> 分隔，按钮生成 button 标签，
      Title 与文本经 Escape 转义）
```

## 备注

- ecd/Command.md 的 HTML 系章节未收录此函数，仅有 zh 套件的简述。
- zh 文档「因为 p 标签没有被附加，所以 ALIGNMENT 指令的对齐方式没有被反映出来」与源码 `DisplayLine2Html(..., false)` 一致。
- zh 文档「检索当前在PRINT等待换行的Html格式的字符串缓冲区，并清空缓冲区」与源码 `PopDisplayingLines()` 的 pop 语义一致。
- 相关函数：`HTML_GETPRINTEDSTR`（取已显示的指定行，带 p/nobr 标签）、`HTML_ESCAPE`（转义字符串）、`HTML_TOPLAINTEXT`（HTML 转纯文本）。
