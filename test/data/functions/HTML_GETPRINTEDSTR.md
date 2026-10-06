# HTML_GETPRINTEDSTR

- **类别**：式中函数（HTML 系函数）
- **签名**：str HTML_GETPRINTEDSTR(`int lineNo`)
- **签名**：str HTML_GETPRINTEDSTR()（参数可省略，省略时 lineNo = 0）
- **文档来源**：zh 套件 `zh/HTML_PRINT.md`「相关函数」→「### # str HTML_GETPRINTEDSTR(int lineNo)」；`ecd/Command.md` 的 HTML 系章节未收录；`ecd/Expression.md` 未收录

## 语义

取得控制台当前显示内容中由 `lineNo` 指定的行，作为 HTML 格式的字符串返回。行的计数方式与 `LINECOUNT`、`CLEARLINE` 指令相同（0 为最旧的显示行）。

返回的字符串带 `<p align='...'>` 与 `<nobr>` 包裹（对齐信息被还原为 p 标签），行内各片段以 `<br>` 等标签表示，按钮部分还原为 button 标签，文本经 HTML 转义。主要用于把已显示的行重新交给 `HTML_PRINT` 复现。

参数 `lineNo` 小于 0 时抛运行时错误（CodeEE）。行号超出当前显示范围时返回空字符串。

## 用法

### str HTML_GETPRINTEDSTR(lineNo)
- lineNo：0 以上的整数，行号（与 LINECOUNT/CLEARLINE 的计数一致）。
- 返回值：该行内容的 HTML 格式字符串；行不存在时为空字符串。
```erb
PRINTL Hello, World!
S = HTML_GETPRINTEDSTR(0)
; S ≈ "<p align='left'><nobr>Hello, World!</nobr></p>"
PRINTL --
HTML_PRINT HTML_GETPRINTEDSTR(0)   ; 把刚显示过的行再画一遍
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:143`（`["HTML_GETPRINTEDSTR"] = new HtmlGetPrintedStrMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5071`（`HtmlGetPrintedStrMethod`）；核心转换在 `UI/Game/HtmlManager.cs:308`（`HtmlManager.DisplayLine2Html`）

```text
HtmlGetPrintedStrMethod:
构造：返回类型 = string；
     参数 = [Int]，OmitStart = 0（整个参数可省略）；
     CanRestructure = false。

GetStrValue(exm, args):
    lineNo = 0
    若 args.Count > 0:
        lineNo = args[0].GetIntValue(exm)
    若 lineNo < 0:
        抛 CodeEE（第 1 参数不能为负值 lineNo）
    dispLines = exm.Console.GetDisplayLines(lineNo)
        ; 取显示缓冲区中第 lineNo 行（与 LINECOUNT 计数一致）
    若 dispLines == null:
        返回 ""
    返回 HtmlManager.DisplayLine2Html(dispLines, true)
        ; needPandN = true：还原对齐标签与 <nobr>

HtmlManager.DisplayLine2Html(lines, needPandN) 概要:
    lines 为空 → 返回 ""
    needPandN 时:
        按第 1 行的 Align 追加 "<p align='left'/'center'/'right'>"
        追加 "<nobr>"
    逐行遍历（行间以 <br> 分隔）:
        对每个 ConsoleButtonString:
            有 Title 时 titleValue = Escape(Title)
            是按钮或有 title 或锁定 PointX → 生成 <button ...> 标签
            文本内容经 Escape 转义后追加
    needPandN 时结尾追加 "</nobr></p>"
    返回拼接结果
```

## 备注

- 本函数与 `HTML_POPPRINTINGSTR` 的差别：本函数从「已显示的行」中取指定行（`GetDisplayLines`，转换时带 p/nobr 对齐标签）；后者从「正在等待换行的输出缓冲区」取内容（`PopDisplayingLines`，转换时 needPandN = false，不带 p 标签）。
- ecd/Command.md 的 HTML 系章节未收录此函数，仅有 zh 套件的简述；「对行的计数与 LINECOUNT 和 CLEARLINE 指令相同」与源码 `GetDisplayLines(lineNo)` 一致。
- 源码中参数可省略（OmitStart = 0），省略等价于 lineNo = 0；zh 文档签名未体现这一点。
