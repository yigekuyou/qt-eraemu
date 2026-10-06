# HTML_STRINGLEN

- **类别**：式中函数（HTML 系函数）
- **签名**：`int HTML_STRINGLEN(str html{, int returnPixel})`
- **文档来源**：`ecd/`、`zh/` 两套中文文档未收录；EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:15-17`（「◆ int HTML_STRINGLEN html(, returnPixel)」）有记载；changelog `emuera.em/Readme/Emuera.EM_changelog.txt:2`「HTML_STRINGLEN機能拡張」（v7，2022-05-18）。语义以源码为准。

## 语义

计算一段 HTML 字符串按当前字体实际显示时的宽度。字符串含多行时只取**第 1 行**的宽度（`HtmlLength` 只累加 `lines[0]` 的按钮宽度）。

第 2 参数决定单位：为 `0` 或省略时用「半角字符宽」为单位（即像素宽度除以 `FontSize/2`，带小数时向上取整）；非 `0` 时直接返回像素数。宽度按当前字体（`Config.FontSize`）与实际排版（`StrMeasure`，含 `<b>` 等加粗导致变宽、`<img>`/`<shape>` 的固有尺寸）计算，与 `HTML_SUBSTRING`／`HTML_STRINGLINES` 使用同一套测量逻辑。

空串或不产生任何显示行的 HTML 返回 `0`。

## 用法

### int HTML_STRINGLEN(html{, returnPixel})
- html：带标签的 HTML 字符串（`HTML_PRINT` 能接受的格式）。
- returnPixel：`0` 或省略 → 半角字符宽（向上取整）；非 `0` → 像素。
- 返回值：第 1 行的显示宽度。
```erb
; 求一段加粗文本的宽度
PRINTL HTML_STRINGLEN("AB<b>CD</b>EFG")        ; 半角字符宽（比 7 大，因为 <b> 更宽）
PRINTL HTML_STRINGLEN("AB<b>CD</b>EFG", 1)     ; 像素宽

; 判断能否放进 30 半角字符宽的版面
IF HTML_STRINGLEN(S) <= 30
    HTML_PRINT S
ELSE
    HTML_PRINT "<b>" + S + "</b>"    ; 太宽就加粗提醒
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:218`（`["HTML_STRINGLEN"] = new HtmlStringLenMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:31`（`HtmlStringLenMethod`）→ `UI/Game/HtmlManager.cs:47`（`HtmlManager.HtmlLength`）

```text
HtmlStringLenMethod:
    构造:
        ReturnType = long
        argumentTypeArrayEx = [ { ArgTypes = { String, Int }, OmitStart = 1 } ]
            # 1~2 个参数：第 2 个可省略
        CanRestructure = true            # 全常量参数时可在解析期折叠
    GetIntValue(exm, arguments):
        len = HtmlManager.HtmlLength(arguments[0] 的字符串值)     # 像素宽度
        若 只给了 1 个参数 或 arguments[1] 的值 == 0:              # 以半角字符宽为单位
            若 len >= 0:
                返回 2*len / FontSize + (2*len % FontSize != 0 ? 1 : 0)     # 向上取整
            否则:                                                  # len < 0 的分支（实际不可达）
                返回 2*len / FontSize - (2*len % FontSize != 0 ? 1 : 0)     # 向下取整
        返回 len                                                  # 像素模式

HtmlManager.HtmlLength(s)（HtmlManager.cs:47）:
    lines = Html2DisplayLine(s, GlobalStatic.Console.StrMeasure, null)   # 解析标签并测量排版
    若 lines.Length <= 0: 返回 0
    len = 0
    遍历 lines[0].Buttons: len += btn.Width       # 只统计第 1 行的按钮宽度
    返回 len
```

## 备注

- 文档（EM readme）与源码一致：第 2 参数 `0`/省略 → 半角字符单位，否则像素；多行时只取第 1 行。readme 未提到「向上取整」，该细节来自源码。
- 换算公式 `2*len/FontSize` 的含义是「一个半角字符宽 = FontSize/2 像素」；`len >= 0` 的取整为向上（不足半角按 1 个算），负分支是源码里不可达的死代码（`HtmlLength` 最差返回 0）。
- `CanRestructure = true`：当两个参数都是常量时，编译器会在解析期就把结果算成常量；因此它不能用于依赖运行时字体大小以外的动态测量（字体尺寸变化后需重新求值）。
- 宽度测量会真实解析标签（`Html2DisplayLine`），因此对格式错误的 HTML 可能抛出与 `HTML_PRINT` 相同的解析期/运行期错误。
