# HTML 显示

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

> 来源：ecd/docs/HTML_PRINT.md、Era-Chinese-Documentation/docs/HTML_PRINT.md（两套文档内容一致，均为 https://osdn.net/projects/emuera/wiki/exhtml 的翻译）；源码 `UI/Game/HtmlManager.cs` 等已实际核对。

## 概述

HTML_PRINT 系是 Emuera 提供的一套「仿 HTML」显示语法：用形如 `<tag attr='值'>文本</tag>` 的标签在一个字符串表达式里同时描述文字、字体、颜色、按钮、图像和形状，然后由引擎解析成内部显示行（`ConsoleDisplayLine`）。它与 PRINT 系是并列的两条输出路径：PRINT 系靠 `ALIGNMENT`、`SETFONT`、`COLOR`、`FONTSTYLE` 等状态指令逐步累积样式，而 HTML_PRINT 完全不受这些指令影响，所有样式必须在标签内声明。解析工作集中在静态类 `HtmlManager`（位于 `UI/Game/HtmlManager.cs`，旧文档路径写的是 `UI/Game/HtmlManager.cs`），命令入口在 `Runtime/Script/Statements/Instraction.Child.cs`。

## HTML_PRINT 命令

`HTML_PRINT <字符串表达式>(, <数值表达式>)`

- 参数不是 PRINT 那样的字面字符串，而是像 PRINTS 一样的字符串表达式；并且自动换行，因此行为类似 `PRINTSL`。
- 可选第 2 参数（EE 扩展）：非 0 时输出进入 PRINT 缓冲区（可被后续 PRINT 拼接），见 `EmueraConsole.PrintHtml(string, bool toPrintBuffer)`（`UI/Game/EmueraConsole.Print.cs:498`）。
- 受 `SkipPrint`（`SKIPDISP`）控制：`HTML_PRINT_Instruction.DoInstruction` 开头检查 `GlobalStatic.Process.SkipPrint`（`Runtime/Script/Statements/Instraction.Child.cs:328`）。

命令本体实现的伪代码（真实行号）：

```text
// Runtime/Script/Statements/Instraction.Child.cs:312-338
class HTML_PRINT_Instruction : AInstruction {
    ctor() { flag = EXTENDED|METHOD_SAFE;
             ArgBuilder = ArgumentParser.GetArgumentBuilder(FunctionArgType.SP_HTML_PRINT); }
    DoInstruction(exm, func, state) {
        if (GlobalStatic.Process.SkipPrint) return;        // :329
        var arg = (SpHtmlPrint)func.Argument;
        if (arg.IsConst) exm.Console.PrintHtml(arg.ConstStr, arg.ConstInt != 0);       // :331
        else             exm.Console.PrintHtml(arg.Str.GetStrValue(exm), ...);         // :332
    }
}
```

参数构建在 `SP_HTML_PRINT_ArgumentBuilder.CreateArgument`（`Runtime/Script/Statements/ArgumentBuilder.cs:355-396`）：第 1 参必须是字符串型，第 2 参必须是整型；参数个数不足/过多按警告处理（`warn(...)`，非致命）。

相关变体命令（文档未提及，源码存在）：

- `HTML_PRINT_ISLAND <字符串表达式>` —— 输出到完全独立的 HTML 层：`exm.Console.PrintHTMLIsland(str)` 把 `HtmlManager.Html2DisplayLine` 的结果加入 `_htmlElementList`（`Runtime/Script/Statements/Instraction.Child.cs:366-386`、`UI/Game/EmueraConsole.Print.cs:136-139`）。
- `HTML_PRINT_ISLAND_CLEAR` —— 清空该层（`UI/Game/EmueraConsole.Print.cs:140-143`）。

与 PRINT 系的关系：

- HTML_PRINT 绘图不受 `ALIGNMENT`、`SETFONT`、`COLOR`、`FONTSTYLE` 等指令影响，效果全部在标签内指定。
- `<p align='...'>` 对应 `ALIGNMENT`；`<nobr>` 对应 `PRINTSINGLE`（不自动换行）；`<button>` 对应 `PRINTBUTTON`。
- 反向转换：`HTML_GETPRINTEDSTR` / `HTML_POPPRINTINGSTR` 可以把已显示/待换行的行转回 HTML 字符串（见下文）。

## 标签子集

总体形式为 `<tagname attr='值'>文本</tagname>`。属性值必须用 `"` 或 `'` 括起；因为 `"` 在 ERB 字符串里有歧义，文档建议用 `'`。标签名大小写不敏感（解析时统一 `tag.ToLower()`，`UI/Game/HtmlManager.cs:963`）。

### p

```html
<p align='right'>文本</p>
```

- `align` 属性必需，取 `left` / `center` / `right`，对应 `ALIGNMENT` 指令（`UI/Game/HtmlManager.cs:989-1024`）。
- `<p>` 只能出现在行头（行内尚未出现任何文本），`</p>` 只能出现在行尾；`</p>` 可省略。重复使用 `<p>`、在非行头使用都会抛 CodeEE。
- 行内出现 `\n` 时按 `<br>` 处理（`UI/Game/HtmlManager.cs:531-535`）。

### nobr

```html
<nobr>文本</nobr>
```

- 相当于 `PRINTSINGLE`：加此标签后超出绘图区导致的隐式换行不会发生（显式 `<br>` 仍有效）。
- Emuera 不能水平滚动，超窗宽的内容看不到。
- `<nobr>` 只能在第一个文本之前，`</nobr>` 只能在最后文本之后；`</nobr>` 可省略（`UI/Game/HtmlManager.cs:980-988`）。

### br

```html
<br>
```

- 换行。多个 `<br>` 在 `CLEARLINE` / `LINECOUNT` 中都各算一行。
- 不允许带属性（`UI/Game/HtmlManager.cs:975-979` 检查 `wc != null` 则报错）。

### button 与 nonbutton

```html
<button value='0' title='提示' pos='300'>按钮文字</button>
<nonbutton title='提示'>非按钮文字</nonbutton>
```

- `button` 把包围文本变成可点击按钮；`nonbutton` 把文本渲染为非按钮。
- `value`：仅 button 可用；未指定 value 的 `<button>` 会被当作 nonbutton 渲染（不可点击）——实现上 `buttonTag.IsButton = value != null`（`UI/Game/HtmlManager.cs:1349`）。value 可以是整数或字符串，整数时按钮返回整数值（`long.TryParse`，`UI/Game/HtmlManager.cs:1345`）。
- `title`：鼠标悬停时的工具提示。
- `pos`：仅在对齐为 left 且行有 `<nobr>` 时可用；指定按钮距行左端的横向位置，单位是字号的百分比。运行末尾统一校验：有 `pos` 但无 `<nobr>` 抛 `CanNotUsePosWithoutNobr`，对齐不是 LEFT 抛 `CanOnlyUsePosAssignmentLR`（`UI/Game/HtmlManager.cs:623-633`）。
- button/nonbutton 不可嵌套（`NestedButtonTag`，`UI/Game/HtmlManager.cs:1283-1284`）；`</button>` 前必须有未闭合的 `<button>`（`UI/Game/HtmlManager.cs:911-922`）。

### font

```html
<font face='ＭＳ ゴシック' color='#FF0080' bcolor='red'>文本</font>
```

- `face`：字体名；空字符串表示用配置字体；字体不存在/不支持时回退到 `Microsoft Sans Serif`。
- `color`：文字颜色，`#RRGGBB` 十六进制或颜色名（.NET `Color` 结构名）；`transparent` 不允许（`stringToColorInt32`，`UI/Game/HtmlManager.cs:1466-1506`）。
- `bcolor`：按钮被选中时的颜色。
- 可嵌套；嵌套时未指定的属性继承外层 `<font>`（`UI/Game/HtmlManager.cs:1443-1453`）；`</font>` 弹出最近一个 font 栈，栈空时报错（`UI/Game/HtmlManager.cs:906-910`）。

### b / i / u / s

```html
<b>加粗</b> <i>斜体</i> <u>下划线</u> <s>删除线</s>
```

- 分别置 Bold / Italic / Underline / Strikeout 字体样式（`UI/Game/HtmlManager.cs:965-974`）。
- 不允许带属性，不允许重复套用同一标签，`</b>` 等必须与已开的标签对应，否则 `UnexpectedCloseTag`。

### img

```html
<img src='资源名' srcb='按钮态资源名' height='200' width='50' ypos='-10'>
```

在一行内显示图像；资源须在资源 CSV 中登记（详见 语言/资源文件 相关文档与 `ecd/Resource.md`）。

- `src`：必需。未指定 height/width 时缩放到高度、宽度都与字号一致（保持长宽比）。绘制接口为 WINAPI 时不做 alpha 混合。
- `srcb`：按钮选中态显示的资源；省略则用 src 的图，缩放到与 src 相同大小。
- `height`：显示高度占字号的百分比，省略为 100；负值垂直翻转。
- `width`：显示宽度占字号的百分比，省略为 0（保持原长宽比）；负值水平翻转。
- `ypos`：显示纵位置占字号的百分比，省略为 0；注意基于字号而非行高。横向位置调整用 `<shape type='space'>` 或按钮的 `pos`。
- EE 扩展：还有 `srcm` 属性（`UI/Game/HtmlManager.cs:1063-1068`）；height/width/ypos 支持 MixedNum（可带 `px` 后缀的绝对像素，`UI/Game/HtmlManager.cs:1246-1251` 是 shape 的解析，img 用同一 ParseMixedNum 机制）。

### shape

```html
<shape type='rect' param='0,25,400,50' color='red' bcolor='blue'>
<shape type='space' param='400'>
```

在行内绘制指定形状。

- `type`：必需，`rect`（矩形）或 `space`（空位）。
- `param`：必需，以字号百分比为单位，多值用逗号分隔，支持 `px` 后缀（`UI/Game/HtmlManager.cs:1246-1251`）。
  - `rect`：param 为 1 个数时指定矩形宽度（`param='400'` 等价 `param='0,0,400,100'`）；为 4 个数时按 x, y, 宽, 高 指定。
  - `space`：留出 param 指定宽度的空白，`param='400'` 大约相当于 4 个全角空格。
- `color` / `bcolor`：形状颜色 / 选中态颜色，格式同 `<font>`。

### 字符实体引用

被 `&` 与 `;` 包围的词视为字符实体引用。文档列出的支持集：`&amp;` `&gt;` `&lt;` `&quot;` `&apos;` 以及 `&#nn;`（十进制）、`&#xnn;`（十六进制，不超过 0xFFFF）。

源码实际还支持 `&nbsp;`（展开为半角空格），`HtmlManager.Unescape`（`UI/Game/HtmlManager.cs:685-753`）：

```text
// UI/Game/HtmlManager.cs:685-753 (节选)
switch (escWord) {
    case "nbsp": b.Append(" "); break;
    case "amp":  b.Append("&"); break;
    case "gt":   b.Append(">"); break;
    case "lt":   b.Append("<"); break;
    case "quot": b.Append("\""); break;
    case "apos": b.Append("\'"); break;
    default: // '#': 10/16 进制 Unicode, 0..0xFFFF, 越界/非法抛 CodeEE
}
// '&~;' 之间为空或连续 '&' 会抛 MissingSemicolon / ContinuouslyAndSemicolon
```

转义函数 `HTML_ESCAPE` 对应 `HtmlManager.Escape`（`UI/Game/HtmlManager.cs:660-683`），把 `& > < " '` 五个字符替换为实体。

### 注释

```html
<!-- 注释 -->
```

`<!--` 与 `-->` 之间的内容被忽略；缺少 `-->` 抛 `NotFoundCloseTag`（`UI/Game/HtmlManager.cs:522-530`）。

### div（EE/改造版扩展，官方文档未记载）

本仓库源码实现了完整的 `<div>` 标签（`UI/Game/HtmlManager.cs:189-213、1090-1195`）：属性有 `xpos`/`ypos`/`width`/`height`（必需后两者，MixedNum，可 px）、`depth`、`color`、`size='w,h'`、`rect='x,y,w,h'`、`bcolor`、`display='relative'|'absolute'`，以及边框盒模型属性（margin/border/padding，经 `TryParseStyledBoxModel`）。div 内部内容递归走 `html2DisplayLine` 形成独立排版子区（`UI/Game/HtmlManager.cs:546-593`）。div 不嵌套、必须闭合，否则 `NestedTag` / `TagIsNotClosed`。

### clearbutton（EE/改造版扩展，官方文档未记载）

`<clearbutton notooltip='true|false'>`：打开后，本行内后续 `<button>` 不再按钮化（渲染为普通文本），`notooltip='true'` 同时禁用工具提示；`</clearbutton>` 恢复（`UI/Game/HtmlManager.cs:1364-1392、923-930、1339-1357`）。

## 相关命令与函数

### HTML_TAGSPLIT

```erb
HTML_TAGSPLIT <字符串表达式>(, <数值变量>, <字符串变量>)
```

把目标字符串按 HTML 标签和纯文本切分：分段数写入 `RESULT`，各段写入 `RESULTS`；指定第 2/3 参时写入指定变量。切分出错时 `RESULT = -1`。不校验标签内容或配对；分段数超过 RESULTS 数组大小时多余部分丢弃。

实现（`Runtime/Script/Statements/Instraction.Child.cs:339-364`、`HtmlManager.HtmlTagSplit`，`UI/Game/HtmlManager.cs:415-443`）：扫描 `<` 与 `>`，标签（含尖括号）与文本交替成段；找不到配对 `>` 时整体返回 null → RESULT=-1。

### str HTML_POPPRINTINGSTR()

取回当前处于「PRINT 等待换行」状态的缓冲区内容（HTML 格式字符串）并清空缓冲。因为不附加 `<p>` 标签，`ALIGNMENT` 的对齐不会被反映。实现：`exm.Console.PopDisplayingLines()` + `HtmlManager.DisplayLine2Html(lines, needPandN:false)`（`Runtime/Script/Statements/Function/Creator.Method.cs:5109-5128`、`UI/Game/HtmlManager.cs:308-413`）。

### str HTML_GETPRINTEDSTR(int lineNo)

把已显示行中第 lineNo 行的内容作为 HTML 字符串取回；行号计数与 `LINECOUNT` / `CLEARLINE` 相同。行号为负抛 CodeEE；`needPandN:true`，即结果带 `<p align='...'><nobr>` 前缀（`Runtime/Script/Statements/Function/Creator.Method.cs:5071-5107`）。

### str HTML_ESCAPE(str value)

把字符串转为 HTML 形式（转换为字符实体引用），即 `HtmlManager.Escape`。逆操作用 `HTML_TOPLAINTEXT`。

### str HTML_TOPLAINTEXT(str value)

把 HTML 字符串转纯文本：删除所有标签、展开字符实体。实现是正则 `\<[^<]*\>` 去标签后 Unescape（`HtmlManager.Html2PlainText`，`UI/Game/HtmlManager.cs:654-658`）。

## 解析主流程伪代码

```text
// UI/Game/HtmlManager.cs:470-652 (html2DisplayLine, 节选)
while (!st.EOS) {
    found = st.Find('<');
    if (文本) { cssList.Add(new ConsoleStyledString(Unescape(txt), state.GetSS()));
                if (state.FlagPClosed)   throw CodeEE("</p> 之后出现了文本");   // :509
                if (state.FlagNobrClosed) throw CodeEE("</nobr> 之后出现了文本"); } // :511
    if (st.CurrentEqualTo("<!--")) { 跳过到 "-->"; 无则 CodeEE; continue; }
    if (st.Current == '\n') { state.FlagBr = true; st.ShiftNext(); }   // \n 视为 <br>
    else { st.ShiftNext(); part = tagAnalyze(state, st);               // :539
           if (st.Current != '>') throw CodeEE("找不到标签终止 '>'");   // :541
           cssList.Add(part); }
    if (FlagBr) { buttonList.Add(cssToButton(...)); buttonList.Add(null); }  // 强制换行
    if (FlagButton && cssList.Count>0) buttonList.Add(cssToButton(...));     // 按钮化
}
if (CurrentDivTag/CurrentButtonTag/FontStyle!=Regular/FonttagList 非空)
    throw CodeEE("存在未关闭的标签");                                        // :612-616
ret = PrintStringBuffer.ButtonsToDisplayLines(buttonList, sm, FlagNobr, ...);
foreach (dl in ret) dl.SetAlignment(state.Alignment);
```

样式状态由 `HtmlAnalzeState` 持有（`UI/Game/HtmlManager.cs:245-301`）：`FontStyle` 位标志、`FonttagList`（font 栈）、`FlagP/FlagNobr/FlagPClosed/FlagNobrClosed`、`Alignment`、按钮标签信息等；最终样式经 `GetSS()`（:279-300）合成 `StringStyle`。

## 限制与错误行为

HTML 解析失败抛的都是 `CodeEE`（脚本侧错误，中断当前脚本），错误文案集中在 `Runtime/Utils/EvilMask/Lang/Error` 资源里，错误索引表（ecd/Error_Index.md）中来源标注为 `UI/Game/HtmlManager.cs` 的条目即属此类，例如：

- `</p>の前に<p>がありません` / `</nobr>の前に<nobr>がありません`（闭合标签不配对）
- `</p>の後にテキストがあります` / `</nobr>の後にテキストがあります`
- `<p>が2度以上使われています` / `<p>が行頭以外で使われています`（nobr 同）
- `<button>又は<nonbutton>が入れ子にされています`
- `<nobr>が設定されていない行ではpos属性は使用できません` / `alignがleftでない行ではpos属性は使用できません`
- `タグ終端'>'が見つかりません` / `閉じられていないタグがあります`
- `終了タグ</...` 无法解释的闭合标签 / `'&'に対応する';'がみつかりません`
- `色を表す単語又は#RRGGBB値が必要です`（颜色无法解释）

通用兜底错误在 `tagAnalyze` 的 `error:` 标签：`HtmlTagError`（"html 文字列 …" + 整行内容，`UI/Game/HtmlManager.cs:1462-1463`）。

## 与源码的差异/备注

1. **div、clearbutton、HTML_PRINT_ISLAND 系未见于文档**。ecd/zh 的 HTML_PRINT 页只覆盖 p/nobr/br/button/nonbutton/font/b/i/u/s/img/shape/实体/注释；本仓库源码额外实现了 `<div>`（含盒模型、px 单位）、`<clearbutton>`、`HTML_PRINT_ISLAND`、`HTML_PRINT_ISLAND_CLEAR`，以及 img 的 `srcm` 属性和 height/width/ypos/shape param 的 `px` 单位。这些均标注 `EM_私家版`（Emuera 改造版扩展），使用时应确认目标引擎。
2. **`&nbsp;` 未见于文档**。文档只列 `&amp; &gt; &lt; &quot; &apos; &#nn; &#xnn;`，源码 `Unescape` 还接受 `nbsp`。
3. **HTML_PRINT 第 2 参数未见于文档**。源码允许 `HTML_PRINT str, num`（非 0 时进入 PRINT 缓冲，`UI/Game/EmueraConsole.Print.cs:498`），文档未记载。
4. 文档称字体回退为 `Microsoft Sans Serif`，这在当前 .NET（Core）代码中属历史说明，源码本身不做字体回退（交给 .NET/GDI 行为）。
5. 错误索引表把 HTML 错误来源标为 `UI/Game/HtmlManager.cs`，本仓库实际路径为 `UI/Game/HtmlManager.cs`（目录重构导致，行文时注意）。
6. 两套文档（ecd 与 zh）的 HTML_PRINT 页内容一致，仅版式/目录不同，无实质差异。
