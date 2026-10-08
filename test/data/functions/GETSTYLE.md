# GETSTYLE

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（ecd/Command.md 以命令口径收录；本仓库仅有函数形态）
- **签名**：int GETSTYLE()
- **文档来源**：`ecd/Command.md`「### GETSTYLE」（文字样式相关小节）；`ecd/Expression.md`（表达式内函数签名列表）；zh 套件未收录

## 语义

返回当前文字样式（字体风格）的位标志整数值。返回值格式与 `SETSTYLE` 指令的参数格式相同。

若从未使用过 `FONTSTYLE`、`FONTBOLD`、`FONTITALIC` 等指令改变样式，则返回 `0`（常规样式）。

各二进制位含义（源码语义）：`1` = 加粗（Bold）、`2` = 倾斜（Italic）、`4` = 删除线（Strikeout）、`8` = 下划线（Underline）；可叠加（如加粗＋倾斜 = 3）。

## 用法

### int GETSTYLE()
- 无参数。
- 返回值：当前样式的位标志（0~15）。
```erb
FONTBOLD 1
FONTITALIC 1
A = GETSTYLE()          ; A = 3（加粗＋倾斜）
PRINTFORML 当前样式 = {A}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:48`（`["GETSTYLE"] = new GetStyleMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2622`（`GetStyleMethod`）

```text
构造：返回类型 = long；参数 = []（无参数）；CanRestructure = false。

GetIntValue(exm, args):
    fontstyle ← GlobalStatic.Console.StringStyle.FontStyle
    ret ← 0
    若 (fontstyle & FontStyle.Bold) == FontStyle.Bold:        ret ← ret | 1
    若 (fontstyle & FontStyle.Italic) == FontStyle.Italic:    ret ← ret | 2
    若 (fontstyle & FontStyle.Strikeout) == FontStyle.Strikeout: ret ← ret | 4
    若 (fontstyle & FontStyle.Underline) == FontStyle.Underline: ret ← ret | 8
    返回 ret
```

## 备注

- ecd/Command.md 的小节按"返回到 `RESULT:0`"的命令口径描述；本仓库 `Runtime/Script/Statements/FunctionIdentifier.cs` 中没有 GETSTYLE 命令注册，只有式中函数形态，结果直接在表达式中取得。
- 文档未列出各标志位的数值；位含义（1/2/4/8）依据源码。`SETSTYLE` 指令按相同位格式解释参数（可交叉参考 `ecd/Command.md` 的 SETSTYLE 小节）。
- 文档提到"若没有使用过 `FONTSTYLE`、`FONTBOLD` 或 `FONTITALIC` 指令则返回 0"，源码行为一致（默认 `FontStyle.Regular`）。
- 与之相对的取字体名函数为 `GETFONT`（返回字符串，`Runtime/Script/Statements/Function/Creator.Method.cs:2645` 附近）。
- zh 套件未收录本函数。
