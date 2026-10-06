# HTML_ESCAPE

- **类别**：式中函数（HTML 系函数）
- **签名**：str HTML_ESCAPE(`str value`)
- **文档来源**：`ecd/Command.md`「HTML系」章节未单列本函数；`ecd/Expression.md` 未收录；zh 套件 `zh/HTML_PRINT.md`「相关函数」→「### # str HTML_ESCAPE(str value)」

## 语义

把目标字符串转换为 HTML 格式，即把 HTML 中的特殊字符替换为字符实体引用（character reference），使其能安全地嵌入 `HTML_PRINT` 的文本或标签属性（如 `title`）中。

转换对应关系（源码 `HtmlManager.repDic`）：`&` → `&amp;`、`<` → `&lt;`、`>` → `&gt;`、`"` → `&quot;`、`'` → `&apos;`。其余字符原样保留。

逆向转换（把实体引用还原为字符、去掉标签）使用 `HTML_TOPLAINTEXT` 函数。

## 用法

### str HTML_ESCAPE(value)
- value：待转义的字符串表达式。
- 返回值：特殊字符已被替换为字符实体引用的字符串。
```erb
S = HTML_ESCAPE("<b>""引用""</b>")
; S = "&lt;b&gt;&quot;引用&quot;&lt;/b&gt;"
HTML_PRINT S   ; 按字面文本显示 "<b>"引用""</b>"，而非被解释为标签
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:146`（`["HTML_ESCAPE"] = new HtmlEscapeMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5140`（`HtmlEscapeMethod`）；核心逻辑在 `UI/Game/HtmlManager.cs:660`（`HtmlManager.Escape`）

```text
HtmlEscapeMethod:
构造：返回类型 = string；参数 = [string]；CanRestructure = false。

GetStrValue(exm, args):
    返回 HtmlManager.Escape(args[0].GetStrValue(exm))

HtmlManager.Escape(str):
    rep = ['&', '>', '<', '"', '\'']        ; 替换目标字符集
    repDic = { '&': "&amp;", '>': "&gt;", '<': "&lt;",
               '"': "&quot;", '\'': "&apos;" }
    index = 0
    b = StringBuilder
    循环当 index < str.Length:
        found = str.IndexOfAny(rep, index)
        若 found < 0:            ; 之后没有需转义的字符
            b.Append(str[index..]); 结束循环
        若 found > index:        ; 之间的普通字符先追加
            b.Append(str[index..found])
        b.Append(repDic[str[found]])   ; 追加对应实体引用
        index = found + 1
    返回 b.ToString()
```

## 备注

- ecd/Command.md 的 HTML 系章节只收录了 `HTML_PRINT` 与 `HTML_TAGSPLIT` 两个命令，`HTML_ESCAPE` 等 HTML 相关函数未收录；其文档出处为 zh 套件 `HTML_PRINT.md` 的「相关函数」小节。
- zh 文档说「将目标字符串转为 Html 格式（转换为字符参考）」，与源码的 5 个字符替换规则一致；zh 文档提及的 `HTML_TOPLAINTEXT` 在本仓库同样已注册（`Runtime/Script/Statements/Function/Creator.cs:146`）。
- 源码注释显示官方本想用 `System.Web.HttpUtility.HtmlEncode`，因框架原因改为手写实现；`&apos;` 是其扩展用法（标准 HTML4 实体表中没有，但浏览器可识别）。
