# HTML_TOPLAINTEXT

- **类别**：式中函数
- **签名**：str HTML_TOPLAINTEXT(str value)
- **文档来源**：`ecd/HTML_PRINT.md`「### str HTML_TOPLAINTEXT(str value)」；zh 套件 `HTML_PRINT.md` 同名小节

## 语义

将目标 html 字符串转换为纯文本。具体来说，html 标签被从字符串中删除，字符参考（字符实体，如 `&amp;`）被扩展还原。常用于把 `HTML_ESCAPE` 转换过的字符串（或含 HTML 标记的打印缓冲内容）还原为纯文本；文档中也称"使用 `HTML_TOPLAINTEXT` 函数来取消注释"。

该函数只在表达式中使用（无同名命令），结果作为返回值返回，不写入 `RESULTS:0`。

## 用法

### str HTML_TOPLAINTEXT(str value)
- `value`：字符串表达式，待转换的 html 字符串。
- 返回值：去掉 html 标签、扩展字符参考后的纯文本字符串。
```erb
S = HTML_TOPLAINTEXT(HTML_ESCAPE("<b>粗体</b>"))  ; S = "<b>粗体</b>"（转义被还原）
S = HTML_TOPLAINTEXT("<p>abc</p>")                ; S = "abc"（标签被删除）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:145`（`["HTML_TOPLAINTEXT"] = new HtmlToPlainTextMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5127`（`HtmlToPlainTextMethod`），核心逻辑在 `UI/Game/HtmlManager.cs:654`（`HtmlManager.Html2PlainText`）

```text
构造：返回类型 = string；参数 = [string]；CanRestructure = false（不可常量折叠）。

GetStrValue(exm, args):
    返回 HtmlManager.Html2PlainText(args[0].GetStrValue(exm))

Html2PlainText(str):
    ret ← 用正则 "\<[^<]*\>" 把 str 中所有 html 标签替换为空串
    返回 HtmlManager.Unescape(ret)   ; 逐个扫描 '&', 把 "&～;" 的字符实体引用还原为对应字符
```

## 备注

- ecd 与 zh 两套 HTML_PRINT.md 内容一致。
- 标签删除用的是正则 `\<[^<]*\>`：不处理嵌套的 `<`，`a<b<c>` 这类串的行为与浏览器解析不同，文档未提及此细节。
- 与 `HTML_ESCAPE`（转义为字符参考）互为逆操作方向：`HTML_TOPLAINTEXT` 负责删除标签并还原转义。
