# HTML_SUBSTRING

- **类别**：式中函数（HTML 系函数）
- **签名**：`str HTML_SUBSTRING(str html, int 长度)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:19-28`（「◆ int HTML_SUBSTRING str, int」）有记载。readme 把返回类型写作 `int`，源码为 `string`（见备注）。语义以源码为准。

## 语义

把一段 HTML 字符串按显示宽度从中间切成两段，切点在「按当前字体排版恰好放得下 `长度` 个半角字符」处。为避免标签被切断，前半段会自动补上与未闭合标签配对的**闭合标签**，后半段会自动补上对应的**开始标签**——因此两段单独拿出来都能作为合法 HTML 交给 `HTML_PRINT`。

结果通过 `RESULTS` 返回：`RESULTS:0` = 前半段，`RESULTS:1` = 剩余部分；函数的返回值就是 `RESULTS:0`（等价于 `RESULTS`）。

第 2 参数的单位是**半角字符宽**（内部乘 `Config.FontSize / 2` 换算成像素）。宽度按 `<b>`/`<i>`/`<s>` 等样式标签、`<img>`/`<shape>` 的固有尺寸、先前的标签状态一起测量（`HtmlLength` 逐字符试排）。若在给定宽度下一个字符都放不下，返回 `["", 原串]`，即 `RESULTS:0 = ""`、`RESULTS:1 = 原串`。

## 用法

### str HTML_SUBSTRING(html, 长度)
- html：待分割的 HTML 字符串。
- 长度：切分宽度，单位半角字符宽。
- 返回值：切出的前半段（同时写入 `RESULTS:0`、`RESULTS:1`）。
```erb
; EM readme 的原始示例
HTML_SUBSTRING "AB<b>CD</b>EFG", 4
PRINTSL RESULTS        ; AB<b>C</b>
PRINTSL RESULTS:1      ; <b>D</b>EFG
```

```erb
; 手动做两行折行显示（第一行 + 余下部分）
S = "这是一段<b>很长很长的</b>文本"
HTML_SUBSTRING S, 20
HTML_PRINT RESULTS        ; 前半段，标签已自动闭合
HTML_PRINT RESULTS:1      ; 后半段，标签已自动重新打开
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:219`（`["HTML_SUBSTRING"] = new HtmlSubStringMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:647`（`HtmlSubStringMethod`）→ `UI/Game/HtmlManager.cs:83`（`HtmlManager.HtmlSubString`，返回 `string[2]`）

```text
HtmlSubStringMethod:
    构造:
        ReturnType = string
        argumentTypeArray = [string, long]        # 恰好 2 个参数
        CanRestructure = false
    GetStrValue(exm, arguments):
        str = arguments[0] 的字符串值
        strs = HtmlManager.HtmlSubString(str, (int)arguments[1].GetIntValue(exm))
        output = GlobalStatic.Process.VEvaluator.RESULTS_ARRAY     # 全局 RESULTS 数组（注意用的是 GlobalStatic，不是 exm）
        n = min(output.Length, strs.Length)                        # 上限 2
        Array.Copy(strs, output, n)                                # 写入 RESULTS:0 / RESULTS:1
        返回 output[0]                                             # == RESULTS:0

HtmlManager.HtmlSubString(str, length)（HtmlManager.cs:83，要点）:
    length = length * Config.FontSize / 2      # 半角字符宽 → 像素
    str = Unescape(str)
    扫描：beginStack 记录待闭合标签，endStack 记录对应的闭合标签；
          只有 <b>/<i>/<s> 标记为「样式标签」（isStyleTag，参与前后缀补全）
    正文段用 GetSubStr(pref, suff, 文本, ref length) 逐字符试排，
        pref/suff 为当前未闭合样式标签的开关串，保证测量结果与真实排版一致
    <br> 在本行处断开（delbr = 1，余下部分跳过 "<br>" 4 个字符）
    <img>/<shape> 按固有宽度整体计入
    若 last == 0（一个字符都没放下）：返回 ["", str]
    前半段 = str[0 .. 断点] + 逐个弹出的结束标签
    后半段 = 逐个弹出的开始标签 + 余下部分
    返回 [前半段, 后半段]
```

## 备注

- **文档与源码的签名冲突**：EM readme 写作 `int HTML_SUBSTRING str, int`（第 1 栏 `int` 应为返回值类型），但源码 `ReturnType = typeof(string)`、`GetStrValue` 返回字符串。以源码为准：返回值是**字符串**。
- 语义据源码与 EM readme 一致（readme 的示例结果 `AB<b>C</b>` / `<b>D</b>EFG` 正是源码「补全闭合/开始标签」的行为）。
- 结果写入 `RESULTS`（`RESULTS:0`、`RESULTS:1`），不写 `RESULT`（整数）也不写 `RESULT:1`；源码用的是 `GlobalStatic.Process.VEvaluator`，与 `exm.VEvaluator` 在实际运行中是同一实例（`test/data/functions/HTML_ESCAPE.md` 同类写法亦如此）。
- 写入上限为 `min(2, RESULTS 数组长度)`：若 `RESULTS` 被 `VariableSize.CSV` 改得只剩 1 个元素，只写 `RESULTS:0`，返回仍是 `RESULTS:0`。
- 当宽度太小导致 `last == 0` 时返回 `["", 原串]`：此时 `RESULTS:0` 为空串、`RESULTS:1` 为原串，`HTML_STRINGLINES` 在这种输入下会陷入不前进的循环（见 `HTML_STRINGLINES.md` 备注）。
- `HtmlSubString` 是公开静态方法，`HTML_STRINGLEN`／`HTML_STRINGLINES`／本函数三者共用同一排版测量路径，故三者对同一输入的分行点一致。
