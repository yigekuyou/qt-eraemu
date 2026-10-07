# HTML_PRINT_ISLAND

- **类别**：EE 扩展命令（Emuera 枚举成员；Emuera.NET 区扩展；两套中文文档未收录，语义据源码推定）
- **签名**：`HTML_PRINT_ISLAND <HTML 字符串表达式>{, <第二参数（整数表达式，本命令不使用）>}`
- **文档来源**：无文档收录。两套中文文档均未收录——`test/data/_extracted/ecd/HTML_PRINT.md` 与 `test/data/_extracted/zh/HTML_PRINT.md` 对 `ISLAND` 的 grep 命中数均为 0（即 HTML_PRINT 页没有提到本命令）；`eraTW/README集/EmueraEE Readme/` 亦无本命令。已有文档 `test/data/language/HTML显示.md:37` 仅点到为止（给出实现位置），本文件是首个完整文档；语义据源码推定。

## 语义

把一段 HTML 字符串解析成显示行，追加到**与正文完全独立的「HTML 岛」层**（源码注释称「完全に独立したHTML」，`UI/Game/EmueraConsole.Print.cs:135`）。

- 与 `HTML_PRINT` 的关系：标签解析器共用（`HtmlManager.Html2DisplayLine`，`UI/Game/HtmlManager.cs:460`），可用标签子集与 `HTML_PRINT.md` 相同；区别是 HTML_PRINT 的输出进入正常打印缓冲/显示行列表（参与滚动、被 `CLEARLINE` 等影响），而本命令的输出进入独立列表 `_htmlElementList`，**不进入正文排版**。
- 绘制位置（源码事实）：每次画面重绘时，在普通文本、CBG 图与 div 等部件之后绘制（`UI/Game/EmueraConsole.cs:1775-1780`，注释「真のHTML描画」），从渲染区顶端 y=0 起按 `Config.LineHeight` 逐行排列，**不随正文滚动**；绘制在工具提示之前，因此 tooltip 仍显示在最上层。
- 生存期：该层只被三类操作清空——`HTML_PRINT_ISLAND_CLEAR`（`UI/Game/EmueraConsole.Print.cs:140-143`）、`ClearDisplay()`（`UI/Game/EmueraConsole.Print.cs:51`，其调用点为初始化 `UI/Game/EmueraConsole.cs:459`、报错/重启路径 `:2567`、返回标题 `:2611`）、以及重绘自身；普通 `PRINT`/`CLEARLINE` 不影响它。
- 语言属性/样式指令（`SETFONT`、`COLOR`、`FONTSTYLE`、`ALIGNMENT`）与该层无关，效果全部由标签内属性指定（与 HTML_PRINT 相同）。
- 跳过中不输出：`GlobalStatic.Process.SkipPrint` 为真时直接返回（与 `HTML_PRINT` 一致，`Runtime/Script/Statements/Instraction.Child.cs:376`）。
- 第二参数：参数构建器与 `HTML_PRINT` 共用（`FunctionArgType.SP_HTML_PRINT`），若给出必须是整数表达式；但本命令的 `DoInstruction` **完全不读取它**（源码只取 `arg.Str`/`arg.ConstStr`）。本仓库中该位置在 `PLAYSOUND` 里被用作重复次数、在 `HTML_PRINT` 里被用作「进入打印缓冲」标志，属各命令自定义用途。
- 同族：`HTML_PRINT_ISLAND_CLEAR` 清空该层（见 `HTML_PRINT_ISLAND_CLEAR.md`）。

## 用法

### `HTML_PRINT_ISLAND <HTML 字符串表达式>`
```erb
;在画面顶端显示一行独立 HTML（不参与正文滚动）
HTML_PRINT_ISLAND "<p align='right'><font color='#FF0000'>HUD：独立层</font></p>"
PRINTL 这里是普通正文……
;清理
HTML_PRINT_ISLAND_CLEAR
```

### 带（被忽略的）第二参数
```erb
;语法合法：第二参数会被解析但本命令不使用
HTML_PRINT_ISLAND "<div>内容</div>", 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:395`（枚举 `HTML_PRINT_ISLAND`，`#region Emuera.NET` 自 `:392`）；`Runtime/Script/Statements/FunctionIdentifier.cs:442`（`addFunction(FunctionCode.HTML_PRINT_ISLAND, new HTML_PRINT_ISLAND_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:366`（`HTML_PRINT_ISLAND_Instruction`，`DoInstruction` `:374-385`）；参数构建 `FunctionArgType.SP_HTML_PRINT`（`Runtime/Script/Statements/ArgumentBuilder.cs:355-399`，`argb` 登记于 `:246`）→ `Runtime/Script/Statements/Argument.cs:75`（`SpHtmlPrint`）
- 控制台：`UI/Game/EmueraConsole.Print.cs:136-139`（`PrintHTMLIsland`）；存储 `UI/Game/EmueraConsole.cs:1652`（`List<ConsoleDisplayLine> _htmlElementList`）；绘制 `:1775-1780`；解析 `UI/Game/HtmlManager.cs:460`（`Html2DisplayLine(str, sm, console)`）

```text
构造 HTML_PRINT_ISLAND_Instruction:
    flag = EXTENDED | METHOD_SAFE
    ArgBuilder = SP_HTML_PRINT（1~2 个参数；第 1 参必须字符串，第 2 参可选且必须整数；
                                不满足则在解析阶段按错误处理，见 ArgumentBuilder.cs:361-399）

DoInstruction(exm, func, state):
    if GlobalStatic.Process.SkipPrint: return            # 跳过中不输出
    arg = (SpHtmlPrint)func.Argument
    if arg.IsConst: str = arg.ConstStr
    else:           str = arg.Str.GetStrValue(exm)
    exm.Console.PrintHTMLIsland(str)                     # 完全忽略 arg.Opt

EmueraConsole.PrintHTMLIsland(html)（EmueraConsole.Print.cs:136）:
    _htmlElementList.AddRange(HtmlManager.Html2DisplayLine(html, stringMeasure, this))

画面重绘（EmueraConsole.cs:1775-1780，普通文本/CBG/div 之后、工具提示之前）:
    y = 0
    foreach element in _htmlElementList:
        element.DrawTo(graph, y, false, false, Config.TextDrawingMode)
        y += Config.LineHeight
```

## 备注

- 两套中文文档均未收录；`test/data/language/HTML显示.md:37` 的既有描述（「输出到完全独立的 HTML 层…`_htmlElementList`」）与本文件一致，本文件补全签名、绘制次序、生存期与参数细节。
- 本命令不主动刷新画面（指令内无刷新调用）；输出在下一次画面重绘时才可见（推定：绘制发生在重绘路径中）。
- 与 `PRINT`/`CLEARLINE` 的隔离是由「独立列表 + 独立绘制循环」实现的；`CLEARLINE` 系命令操作的正文行列表与 `_htmlElementList` 无关（`ClearDisplay` 才同时清空两者，见源码调用点）。
- 参数构建器的报错级别为 2（致命），因此「第 1 参非字符串」「多余参数」都会导致解析错误，而不是静默忽略（与 `HTML_PRINT.md` 记载的同一构建器行为一致）。
- 名字中的 ISLAND（孤岛）即「独立层」之意。
