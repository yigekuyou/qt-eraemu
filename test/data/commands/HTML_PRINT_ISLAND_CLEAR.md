# HTML_PRINT_ISLAND_CLEAR

- **类别**：EE 扩展命令（Emuera 枚举成员；Emuera.NET 区扩展；两套中文文档未收录，语义据源码推定）
- **签名**：`HTML_PRINT_ISLAND_CLEAR`（无参数）
- **文档来源**：无文档收录。两套中文文档均未收录——`test/data/_extracted/ecd/HTML_PRINT.md` 与 `test/data/_extracted/zh/HTML_PRINT.md` 对 `ISLAND` 的 grep 命中数均为 0；`eraTW/README集/EmueraEE Readme/` 亦无本命令。已有文档 `test/data/language/HTML显示.md:38` 仅点到为止（给出实现位置），本文件是首个完整文档；语义据源码推定。

## 语义

清空 `HTML_PRINT_ISLAND` 建立的「HTML 岛」层（独立显示行列表 `_htmlElementList`）。

- 一次性清空全部内容（`_htmlElementList.Clear()`），不是「清除最后一行」；没有「逐行清除」的变体。
- 指令内不调用画面刷新（源码只做 `Clear()`），因此内容在下一次画面重绘时才真正消失（推定：清空立即完成，可见性取决于重绘时机）。
- 另一条清空路径是 `ClearDisplay()`（初始化、报错/重启、返回标题时调用），它同时清空正文行列表与岛层（`UI/Game/EmueraConsole.Print.cs:45-64`，岛层清空在 `:51`）。
- 与正文无关：本命令不影响正常打印的行（`CLEARLINE` 等才是正文的清行命令），也不影响工具提示。
- 参数：无。参数构建器为 `VOID`，多写参数只会得到一条**非致命**警告（`Runtime/Script/Statements/ArgumentBuilder.cs:608-609`，级别 1），命令仍照常执行。

## 用法

### `HTML_PRINT_ISLAND_CLEAR`
```erb
HTML_PRINT_ISLAND "<p>临时 HUD</p>"
;……若干处理……
HTML_PRINT_ISLAND_CLEAR
;岛层清空，下一次重绘后画面不再显示该 HUD
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:396`（枚举 `HTML_PRINT_ISLAND_CLEAR`，`#region Emuera.NET` 自 `:392`）；`Runtime/Script/Statements/FunctionIdentifier.cs:443`（`addFunction(FunctionCode.HTML_PRINT_ISLAND_CLEAR, new HTML_PRINT_ISLAND_CLEAR_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:388`（`HTML_PRINT_ISLAND_CLEAR_Instruction`，`DoInstruction` `:396-399`）；参数构建 `FunctionArgType.VOID`（`Runtime/Script/Statements/ArgumentBuilder.cs:602-612`，`argb` 登记于 `:181`）
- 控制台：`UI/Game/EmueraConsole.Print.cs:140-143`（`ClearHTMLIsland`）；被一并清空于 `:51`（`ClearDisplay`，调用点 `UI/Game/EmueraConsole.cs:459`、`:2567`、`:2611`）

```text
构造 HTML_PRINT_ISLAND_CLEAR_Instruction:
    flag = EXTENDED | METHOD_SAFE
    ArgBuilder = VOID（无参数；多写参数只警告不报错）

DoInstruction(exm, func, state):
    exm.Console.ClearHTMLIsland()

EmueraConsole.ClearHTMLIsland()（EmueraConsole.Print.cs:140）:
    _htmlElementList.Clear()

关联（EmueraConsole.Print.cs:45 ClearDisplay）:
    CBProc.ClearScreen(); displayLineList.Clear(); _htmlElementList.Clear();
    ConsoleEscapedParts.Clear(); logicalLineCount = 0; ...
```

## 备注

- 两套中文文档均未收录；`test/data/language/HTML显示.md:38` 的既有描述（「清空该层」）与本文件一致，本文件补全调用点、参数行为与生存期细节。
- 与 `HTML_PRINT_ISLAND.md` 成对：一个追加行、一个整体清空；HTML 标签解析与绘制位置说明见该文件。
- 参数构建器 `VOID` 的告警级别为 1（非致命），所以误写参数不会让脚本解析失败（与 `HTML_PRINT_ISLAND` 的严格参数校验形成对比）。
- 本命令等同于「清空独立层」，不会恢复被 `HTML_PRINT_ISLAND` 覆盖前的画面内容（岛层是叠加绘制，没有备份）。
