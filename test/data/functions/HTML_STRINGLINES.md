# HTML_STRINGLINES

- **类别**：式中函数（HTML 系函数）
- **签名**：`int HTML_STRINGLINES(str html, int 每行宽度)`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）、EE readme、本分支 EM readme 均未收录本函数（EM readme 只收录了 `HTML_STRINGLEN` 与 `HTML_SUBSTRING`）；语义完全据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:666` 的 `HtmlStringLinesMethod` 与 `UI/Game/HtmlManager.cs:83` 的 `HtmlSubString`）。

## 语义

把 `html` 按给定行宽反复切分（调用 `HTML_SUBSTRING` 所用的同一个 `HtmlManager.HtmlSubString`），统计它一共会被折成多少行，作为 `HTML_STRINGLEN` 的「行数版」补充。`<br>` 也计为一次换行（`HtmlSubString` 遇到 `<br>` 即在此处断开）。

第 2 参数的单位与 `HTML_SUBSTRING` 相同：**半角字符宽**（内部会乘 `Config.FontSize / 2` 换算成像素）。空串直接返回 `0`。

注意它测的是「按指定宽度排版需要几行」，与终端实际行宽无关——要按当前窗口宽度算，需要自己用 `CLIENTWIDTH` 等估算后传入。

## 用法

### int HTML_STRINGLINES(html, 每行宽度)
- html：带标签的 HTML 字符串。
- 每行宽度：半角字符宽单位的行宽。
- 返回值：折行后的行数；`html` 为空时返回 `0`。
```erb
S = "AB<b>CD</b>EFG"
PRINTL HTML_STRINGLINES(S, 4)      ; 按 4 半角字符宽排版需要几行
PRINTL HTML_STRINGLINES("<br>", 10) ; <br> 也算换行

; 用行数估算输出高度，决定是否分屏
L = HTML_STRINGLINES(LONGTEXT, 60)
IF L > 20
    ; 太长，走分页
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:220`（`["HTML_STRINGLINES"] = new HtmlStringLinesMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:666`（`HtmlStringLinesMethod`）→ `UI/Game/HtmlManager.cs:83`（`HtmlManager.HtmlSubString`）

```text
HtmlStringLinesMethod:
    构造:
        ReturnType = long
        argumentTypeArray = [string, long]      # 恰好 2 个参数（不可省略）
        CanRestructure = false                  # 依赖运行时字体/排版，不可在解析期折叠
    GetIntValue(exm, arguments):
        str = arguments[0] 的字符串值
        若 str 为 null 或空串: 返回 0
        ret = 0
        循环:
            parts = HtmlManager.HtmlSubString(str, (int)arguments[1].GetIntValue(exm))
                    # parts[0] = 本行（含自动补全的闭合标签）
                    # parts[1] = 剩余部分（含自动补全的开始标签）
            str = parts[1]
            ret++
        直到 str 为空串
        返回 ret

HtmlManager.HtmlSubString(str, length)（HtmlManager.cs:83，要点）:
    length = length * Config.FontSize / 2        # 半角字符宽 → 像素
    str = Unescape(str)
    顺序扫描标签；<b>/<i>/<s> 记入开始/结束栈，其余标签只入栈不入样式栈
    遇到 <br>：delbr = 1 并立即结束本行
    遇到 <img>/<shape>：整块计入宽度，若放不下且本行已有内容则结束本行
    正文按 GetSubStr 逐字符测量（调用 HtmlLength），放不下即断行
    返回 ["前半段 + 补全的闭合标签", "补全的开始标签 + 剩余部分"]
    若一个字符都放不下（last == 0）：返回 ["", str]，即剩余部分仍是原串
```

## 备注

- 语义据源码；两套中文文档与两种 readme 均无此函数（EM readme 只写了 `HTML_STRINGLEN`、`HTML_SUBSTRING`）。它很可能是 `HTML_SUBSTRING` 的配套工具函数，发布时漏写文档。
- 与 `HTML_SUBSTRING` 共用同一测量核心，因此两者对同一 (html, 宽度) 的分行点完全一致——`HTML_STRINGLINES` 等价于「反复调用 `HTML_SUBSTRING` 直到剩余为空」的计数（源码正是这样实现）。
- 若某个字符因宽度限制完全放不下，`HtmlSubString` 返回 `["", 原串]`，此时循环因剩余串非空而继续，且剩余串不变 → **可能死循环**（例如行宽为 0 时传非空 html）。源码没有防护；调用方须保证宽度 ≥ 1（此风险点据源码分支推得，标注为推定，未实际运行验证）。
- 行宽单位是半角字符宽，不是像素；像素宽度的测量请用 `HTML_STRINGLEN(html, 1)`。
- 不计入终端右侧留白、行首缩进等实际布局因素，只反映「按该宽度折行需要几行」。
