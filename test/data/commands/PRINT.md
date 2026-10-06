# PRINT

- **类别**：命令（Emuera 内建，非 EE 扩展；共 54 个枚举成员由本文件统一覆盖——另有 15 个 `PRINTSINGLE` 族成员见 `PRINTSINGLE.md`，二者合计 69 个后缀变体）
- **签名**：
  - `PRINT(|V|S|FORM|FORMS)(|K|D)(|L|W) <参数>`（45 个成员）
    - 无值类型选择器：`PRINT <文本>`（行剩余部分整体作为字面文本）
    - `V`：`PRINTV <数值表达式>[, <数值表达式> …]`
    - `S`：`PRINTS <字符串表达式>`
    - `FORM`：`PRINTFORM <FORM格式文本>`
    - `FORMS`：`PRINTFORMS <FORM格式字符串表达式>`
  - `PRINT(|V|S|FORM|FORMS)N <同上参数>`（5 个成员：`PRINTN` / `PRINTVN` / `PRINTSN` / `PRINTFORMN` / `PRINTFORMSN`；N 不能与 L/W 组合，也不能与 C/LC 组合）
  - `PRINT(|FORM)(C|LC) <文本>|<FORM格式文本>`（4 个成员：`PRINTC` / `PRINTLC` / `PRINTFORMC` / `PRINTFORMLC`）
  - 54 个可写名字的完整一览见下文「## 用法」的变体表；`PRINTCK`／`PRINTCD`／`PRINTFORMCK` … 等带 K/D 的定宽变体属 EE 扩展，另有独立文档（`PRINTCK.md`、`PRINTCD.md`、`PRINTFORMCD.md`、`PRINTFORMCK.md`、`PRINTFORMLCD.md`、`PRINTFORMLCK.md`、`PRINTLCK.md`、`PRINTLCD.md`）
- **文档来源**：`ecd/docs/translation/Command.md`「## PRINT系列」「### PRINT系列指令辅助选择器」「### PRINT(|V|S|FORM|FORMS)(|K|D)(|L|W)」「### PRINT(|FORM)(C|LC)(|K|D)」（权威语义描述）；`Era-Chinese-Documentation/docs/translation/Command.html`（提取本 `_extracted/zh/Command.md`）「## # Print系」「### # Print(|V|S|Form|FormS)(|K|D)(|L|W)」「### # Print(|Form)(C|LC)(|K|D)」。两套文档的命令总表（`ecd/docs/translation/ERB_Commands.md:131`、`:133`）只以「PRINT」两条基名列出该族，而权威清单 `test/data/emuera_standard_cmds.txt` 已逐个收录枚举中的全部变体名（标准清单 97 个 PRINT 族 + EE 清单 5 个 `*N` 变体）——本文件把这些变体的语义按基名统一说明（语义以源码为准）

## 语义

`PRINT` 族是 Emuera（以及 eramaker）最基本的文字输出指令。一条 PRINT 系命令由**基名 + 最多三个后缀位**组成，后缀位共同决定「参数怎么解析」「用什么颜色画」「画完后换不换行 / 等不等输入」。

### 命令名的结构（后缀位，按书写顺序）

```text
PRINT [值类型: 无|V|S|FORM|FORMS] [对齐: 无|C|LC] [配色: 无|K|D] [N] [行尾: 无|L|W]
```

- 实际存在的枚举名只覆盖部分组合：**对齐位（C/LC）只与「无值类型」或 `FORM` 组合**；**N 位只与「无配色」的 5 种值类型组合**（N 与 L/W 互斥）。因此不能写 `PRINTKD`、`PRINTSNL`、`PRINTSINGLEC` 之类的名字——它们不是已登记的枚举成员，写出来会被当成未定义命令。
- 后缀解析由 `PRINT_Instruction` 的构造函数按上述顺序逐段匹配，且要求「吃完后缀后必须恰好到字符串末尾」，否则抛 `ExeEE("PRINT異常")`（`Runtime/Script/Statements/Instraction.Child.cs:87-174`，抛错在 `:172-173`；错误文案 `Runtime/Utils/EvilMask/Lang.cs:746`）。
- 命令名大小写是否敏感取决于配置 `Config.IgnoreCase`（`Runtime/Script/Statements/FunctionIdentifier.cs:43`：`funcDic` 字典使用 `StringComparer.OrdinalIgnoreCase` 与否由该配置决定）。

### 值类型选择器（第 1 位）

| 后缀 | 参数形态 | 参数构造器 / 参数类型 | 求值行为 |
|---|---|---|---|
| 无 | `<文本>` | `STR_ArgumentBuilder(true)` / `FunctionArgType.STR_NULLABLE`（`Runtime/Script/Statements/ArgumentBuilder.cs:614`） | 行剩余部分**整体当作字面文本**，不做变量/表达式解析（源码：`rowStr = st.Substring()`，`Runtime/Script/Statements/ArgumentBuilder.cs:639`）。省略参数时输出空串 |
| `V` | `<数值表达式>[, <数值表达式> …]` | `SP_PRINTV_ArgumentBuilder` / `SP_PRINTV`（`Runtime/Script/Statements/ArgumentBuilder.cs:499`） | 逗号分隔多个项：结果类型为 long 的项按十进制整数拼接，字符串项原样拼接（`Runtime/Script/Statements/Instraction.Child.cs:189-201`）。省略参数等价于空串（不报错）；某一项写成空（如 `PRINTV 1,`）会警告「第 n 引数を省略することはできません」并把该行标为错误行（`Runtime/Script/Statements/ArgumentBuilder.cs:508-512`） |
| `S` | `<字符串表达式>` | `STR_EXPRESSION_ArgumentBuilder(false)` / `STR_EXPRESSION`（`Runtime/Script/Statements/ArgumentBuilder.cs:1451`） | 恰好 1 个字符串表达式，执行时求值。无参数 → 「引数が足りません」错误行；多于 1 个 → 「引数が多すぎます」（`Runtime/Script/Statements/ArgumentBuilder.cs:60-73`、`:1451-1475`） |
| `FORM` | `<FORM格式文本>` | `FORM_STR_ArgumentBuilder(true)` / `FORM_STR_NULLABLE`（`Runtime/Script/Statements/ArgumentBuilder.cs:660`） | 行剩余部分按 FORM 语法解析（`{}` 内为数值表达式、`%%` 内为字符串表达式），**执行时**求值；整段是常量时走 `IsConst` 快速路径（`Runtime/Script/Statements/Instraction.Child.cs:187-188`、`Runtime/Script/Statements/ArgumentBuilder.cs:689-698`） |
| `FORMS` | `<FORM格式字符串表达式>` | `STR_EXPRESSION` + 运行期二次解析（`Runtime/Script/Statements/Instraction.Child.cs:113-118`） | 先求值出字符串，再对该字符串**做转义处理（`CheckEscape`）后当作 FORM 文本解析执行**（`Runtime/Script/Statements/Instraction.Child.cs:205-211`；`ExpressionMediator.CheckEscape` 在 `:80-116`，把 `\`→`\\`、`\{`/`\}`/`\%`/`\@` 保护成字面量） |

> 注：`PRINTS <字符串表达式>` 与 `PRINT <文本>` 的差别正在于前者走表达式求值（`PRINTS "abc" + TOSTR(X)` 合法），后者是字面文本（`PRINT 1 + 2` 原样显示 `1 + 2`）。

### 配色选择器（第 2 位）

执行期先无条件设 `Console.UseUserStyle = true` 与 `Console.UseSetColorStyle = !IsPrintDFunction()`（`Runtime/Script/Statements/Instraction.Child.cs:184-185`），`Console.Style` 的取值逻辑在 `UI/Game/EmueraConsole.Print.cs:75-88`。

| 后缀 | 含义 | 实现 |
|---|---|---|
| 无 | 忽略 `FORCEKANA`，使用 `SETCOLOR` 指定的颜色绘制 | `UseSetColorStyle = true` → 取 `userStyle`（受 SETCOLOR 影响） |
| `K` | 应用 `FORCEKANA`（假名/半角全角转换）后绘制 | 置 `ISPRINTKFUNC`（`Runtime/Script/Statements/Instraction.Child.cs:141-145`）；执行时 `str = exm.ConvertStringType(str)`（`:213-214`）。转换参数来自 `FORCEKANA` 命令：`1`→片假名、`>1`→平假名、`3`→同时半角转全角（`Runtime/Script/Statements/ExpressionMediator.cs:33-45`、`:64-78`）。若从未设置过 `FORCEKANA`（全 0），`K` 后缀与不写等效 |
| `D` | 忽略 `SETCOLOR`，用设置文件里的默认字色绘制 | 置 `ISPRINTDFUNC`（`Runtime/Script/Statements/Instraction.Child.cs:146-150`）→ `UseSetColorStyle = false` → `Style` 取默认色（`:83-86`） |

`K` 与 `D` **不能同时指定**——原因是登记的枚举名里不存在同时含二者的成员（解析器本身是顺序匹配，若能造出这种名字也会被接受）。（ecd 文档：「从 1.736 版本开始，关键词 K 和 D 不能同时指定」。）

### 行尾选择器（第 3 位）与 N

| 后缀 | 行为 | 源码 |
|---|---|---|
| 无 | 输出后**不换行、不等待**（文本先进入输出缓冲，等后续换行类操作/缓冲刷新时才成为显示行） | `Runtime/Script/Statements/Instraction.Child.cs:168-171`（`else → METHOD_SAFE`），`OutputToConsole` 走 `Console.Print(str, lineEnd: true)` |
| `L` | 输出后换行 | 置 `PRINT_NEWLINE | METHOD_SAFE`（`:157-162`）；`OutputToConsole` 调 `Console.NewLine()`（`Runtime/Script/Statements/ExpressionMediator.cs:54-56`） |
| `W` | 输出后**换行并等待回车**（= `WAIT`，等待的是回车键而非任意键） | 置 `PRINT_NEWLINE | PRINT_WAITINPUT`（`:163-167`）；`NewLine()` 后 `Console.ReadAnyKey()`（`Runtime/Script/Statements/ExpressionMediator.cs:57-58`；`ReadAnyKey` 在 `UI/Game/EmueraConsole.cs:661-675`，`anykey=false` → `InputType.EnterKey`） |
| `N` | 输出后**不换行、但等待回车**；且该显示行被标记为「非行尾」（`IsLineEnd=false`），**下一次输出会接到同一显示行上** | 置 `PRINT_WAITINPUT` 且 `isLineEnd = false`（`:151-156`，枚举注释「改行をしないで入力待ち」在 `Runtime/Script/Statements/BuiltInFunctionCode.cs:398`）；`Print(str, lineEnd:false)` → `PrintStringBuffer.isLastLineEnd = false`（`UI/Game/PrintStringBuffer.cs:62-64`）→ flush 出的显示行 `IsLineEnd=false`（`:172`）→ 下一次 `addDisplayLine` 检测到上一行 `!IsLineEnd` 时把上一行删掉、新内容接在其后（`UI/Game/EmueraConsole.Print.cs:217-224`） |

- `W` 系（`PRINTW`/`PRINTVW`/`PRINTSW`/`PRINTFORMW`/`PRINTFORMSW`/`PRINTKW`…/`PRINTDW`…）**不带 `METHOD_SAFE`**，因此在 `#FUNCTION` 内的使用会在装载期告警「"{0}"命令は#FUNCTION中で使うことはできません」（`Runtime/Script/Loader/ErbLoader.cs:923-926`；文案 `Runtime/Utils/EvilMask/Lang.cs:828`）。同样地，等待输入的命令不能在调试控制台里执行（`UI/Game/EmueraConsole.cs:2146-2149`）。
- **源码怪癖**：`N` 系因为有 `N` 分支之后的 `L`/`W` 检测落到 `else`，仍然会被加上 `METHOD_SAFE`，也就是说 `PRINTN` 等**在 `#FUNCTION` 内是被允许的**，尽管它同样会等待输入（`Runtime/Script/Statements/Instraction.Child.cs:151-171`）。

### C / LC（定宽对齐，4 个成员）

`PRINTC` / `PRINTLC` / `PRINTFORMC` / `PRINTFORMLC` 是把文本补齐到「PRINTC 文字长度」（配置项 `Config.PrintCLength`，默认 **25**，`Runtime/Config/ConfigData.cs:65`）后再输出的变体，常用于把 `[100] 项目名称(价格)` 之类文本按钮排成整齐的列。ecd 文档说「在脚本中绘制文本按钮时建议考虑使用 PRINTC 指令」。

| 名称 | 对齐 | 填充算法（`CreateTypeCString`，`UI/Game/EmueraConsole.Print.cs:547-594`） |
|---|---|---|
| `PRINTC` / `PRINTFORMC` | 右对齐（左侧填充，`alignmentRight = true`） | 若字符串的 **Shift-JIS 字节长度** < `PrintCLength`，在左侧补足 `PrintCLength - 长度` 个半角空格；若补齐后显示宽度超过「`PrintCLength` 个半角空格」的宽度，则从左端逐个删空格直到不超宽（`:569-580`） |
| `PRINTLC` / `PRINTFORMLC` | 左对齐（右侧填充，`alignmentRight = false`） | 若长度 < `PrintCLength + 1`，在右侧补足到 `PrintCLength + 1` 个字节宽；同样按显示宽度从右端回收多余空格（`:581-592`） |

- 字节长度用 `Encoding.GetEncoding("Shift-JIS").GetByteCount(str)` 计算（`:554-557`，EE 版在此修掉了 .NET 7 下按 UTF-16 计数导致 PRINTC 宽度错乱的旧缺陷）。
- 字色/字体无法创建时（`new Font(...)` 抛异常）直接原样返回、不填充（`:560-567`）。
- 字符串长度已达阈值时**不填充也不截断**，原样输出。
- `PRINTC` 输出时把行尾标记置为真（`printBuffer.Append(..., lineEnd: true)`，`:530`），但**本身不产生换行**：要配合 `PRINTL`（或 `PRINTCPERLINE` 的计数）自行收行；空串直接返回、什么都不输出（`:527-528`）。
- 一行的 PRINTC 并列数量由设置「PRINTC を並べる数」（`Config.PrintCPerLine`，默认 3，`Runtime/Config/ConfigData.cs:64`）描述，可用 `PRINTCPERLINE` 读取（参见 `PRINTCPERLINE.md`）。

### 全局副作用与限制

- **SKIPDISP**：`SKIPDISP` 非 0 时，所有带 `IS_PRINT` 标志的 PRINT 系一律被跳过（`Runtime/Script/Process.ScriptProc.cs:46-55` 的通用判定 + 各指令内的 `if (GlobalStatic.Process.SkipPrint) return;`，`Runtime/Script/Statements/Instraction.Child.cs:182-183`）；被跳过时连换行与等待也一并跳过。若被跳过的是 `INPUT` 等带 `IS_INPUT` 的命令则直接报错终止（`Runtime/Script/Process.ScriptProc.cs:48-53`；EE readme 记载「PRINT 系命令大半无视 SKIPDISP」是实现遗漏，后已修正）。
- **按钮化**：普通 PRINT 系文本在转成显示行时会按 `[数字] 文本` 的模式自动按钮化（`UI/Game/ButtonStringCreator.cs`；缓冲转按钮在 `PrintStringBuffer.fromCssToButton`，`:304-315`）。要禁止按钮化用 `PRINTPLAIN`。
- **对齐**：输出行沿用 `ALIGNMENT` 的当前设置（flush 时 `line.SetAlignment(alignment)`，`UI/Game/EmueraConsole.Print.cs:210-213`）。
- **缓冲上限**：单行缓冲超过 2000 字（`BufferStrLength > 2000`）后不再追加，并显示「バッファーの文字数が2000字(全角1000字)を超えています」（`UI/Game/PrintStringBuffer.cs:65-74`；文案 `Runtime/Utils/EvilMask/Lang.cs:1052`）。
- **可用上下文**：除 `W` 系外都带 `METHOD_SAFE`，可在 `#FUNCTION` 中使用；`PRINTDATA` 那种 `PARTIAL`（不能紧跟 `SIF`）限制**不适用**于 PRINT 族。

### 同族 / 相邻命令的分工

| 命令 | 关系 |
|---|---|
| `PRINTSINGLE` 族（15 个成员） | 与 `PRINTL` 类似，但**整行不折行**，超出窗口宽度的部分不绘制；见 `PRINTSINGLE.md` |
| `PRINTPLAIN` / `PRINTPLAINFORM` | 输出**纯文本**、不做按钮化（`Runtime/Script/Process.ScriptProc.cs:145-155`）；无任何后缀，不换行不等待 |
| `PRINTBUTTON(|C|LC)` | 强制生成按钮（第 1 参数为显示文本、第 2 参数为点击后送入的输入值）；见 `PRINTBUTTON.md` |
| `PRINTDATA(|K|D)(|L|W)` | 在 `DATA`/`DATAFORM`/`DATALIST~ENDLIST` 候选中随机选一条显示（块结构，`PRINT_DATA_Instruction`）；见 `PRINTDATA.md` |
| `PRINTCPERLINE` | 读取「PRINTC 并列数量」设置；见 `PRINTCPERLINE.md` |
| `REUSELASTLINE` / `CLEARLINE` | 重写/删除已输出的最后一行，常与 `INPUT` 配合（`Runtime/Script/Statements/Instraction.Child.cs:661-690`）；`REUSELASTLINE` 用的是 `PrintSingleLine(str, temporary: true)`，即与 PRINTSINGLE 同一个单行原语、但带「可被替换」标记 |
| `DEBUGPRINT` / `DEBUGPRINTL` / `DEBUGPRINTFORM` / `DEBUGPRINTFORML` | 输出到调试控制台，不受 `SKIPDISP` 影响（`Runtime/Script/Statements/Instraction.Child.cs:541+`） |
| `PUTFORM` | 把 FORM 文本放进 p 缓冲，随下一次换行一起输出（与 PRINT 族拼接显示） |

## 用法

### `PRINT <文本>` / `PRINTL <文本>` / `PRINTW <文本>`

无选择器时参数是**原样文本**（不是表达式）。

```erb
PRINT 请输入数字：
PRINTL
PRINTL 这一整行是字面文本，{1+2} 不会被求值
PRINTW 按回车继续
```

### `PRINTV <数值表达式>[, <数值表达式> …]`

```erb
PRINTV 1 + 2, " 与 ", LOCAL:0
PRINTVL                 ; 只有换行（无参数）
PRINTVW RESULT, " 点"   ; 显示后换行并等待回车
```

### `PRINTS <字符串表达式>`

```erb
PRINTS "处理中…… " + TOSTR(LCNT) + "％ 完成"
PRINTSL
PRINTSW "按回车继续"
```

### `PRINTFORM <FORM格式文本>` / `PRINTFORMS <FORM格式字符串表达式>`

```erb
PRINTFORML 好感度：{LOCAL:0} / 名字：%NAME:MASTER%
PRINTS S = "结果 {RESULT} 点"      ; 先拼字符串
PRINTFORMS S                       ; 再把字符串当 FORM 文本展开
PRINTFORMSN "计算完成：" + TOSTR(RESULT)   ; 不换行等待输入
```

### `PRINTC` / `PRINTLC` / `PRINTFORMC` / `PRINTFORMLC`（定宽）

```erb
REPEAT 4
    PRINTC "[100] 返回"        ; 右对齐（左侧补空格）到 25 字宽
    PRINTLC "[200] 结束"       ; 左对齐（右侧补空格）
    PRINTL
REND
PRINTFORMC "[{LOCAL}] {ITEMNAME:LOCAL}"
PRINTL
```

### 变体总表（54 个可写名字）

行 = 配色 + 行尾，列 = 值类型选择器；表中即完整命令名：

| 配色 | 行尾 | 无（`<文本>`） | `V`（数值表达式） | `S`（字符串表达式） | `FORM`（FORM 文本） | `FORMS`（FORM 字符串表达式） |
|---|---|---|---|---|---|---|
| 无 | 无（默认） | `PRINT` | `PRINTV` | `PRINTS` | `PRINTFORM` | `PRINTFORMS` |
| 无 | `L` | `PRINTL` | `PRINTVL` | `PRINTSL` | `PRINTFORML` | `PRINTFORMSL` |
| 无 | `W` | `PRINTW` | `PRINTVW` | `PRINTSW` | `PRINTFORMW` | `PRINTFORMSW` |
| `K` | 无 | `PRINTK` | `PRINTVK` | `PRINTSK` | `PRINTFORMK` | `PRINTFORMSK` |
| `K` | `L` | `PRINTKL` | `PRINTVKL` | `PRINTSKL` | `PRINTFORMKL` | `PRINTFORMSKL` |
| `K` | `W` | `PRINTKW` | `PRINTVKW` | `PRINTSKW` | `PRINTFORMKW` | `PRINTFORMSKW` |
| `D` | 无 | `PRINTD` | `PRINTVD` | `PRINTSD` | `PRINTFORMD` | `PRINTFORMSD` |
| `D` | `L` | `PRINTDL` | `PRINTVDL` | `PRINTSDL` | `PRINTFORMDL` | `PRINTFORMSDL` |
| `D` | `W` | `PRINTDW` | `PRINTVDW` | `PRINTSDW` | `PRINTFORMDW` | `PRINTFORMSDW` |
| 无 | `N`（不换行+等待） | `PRINTN` | `PRINTVN` | `PRINTSN` | `PRINTFORMN` | `PRINTFORMSN` |

定宽（C/LC）另有 4 个成员：`PRINTC`、`PRINTLC`、`PRINTFORMC`、`PRINTFORMLC`。

```erb
;组合示例
PRINTSDW  "默认色绘制、不换行、等待回车"     ; S + D + W
PRINTKL   "应用 FORCEKANA 后换行"            ; K + L
PRINTN    请输入：                           ; 等待回车，且输入后后续输出接在同一行
PRINTFORMN 请输入数值：
INPUT
```

## 源码实现（emuera.em/Emuera）

- 枚举：`Runtime/Script/Statements/BuiltInFunctionCode.cs:17-19`（`PRINT`/`PRINTL`/`PRINTW`）、`:21-23`（`V`）、`:25-27`（`S`）、`:29-31`（`FORM`）、`:33-35`（`FORMS`）、`:37`（`PRINTC`）、`:115-117`（`PRINTLC`/`PRINTFORMC`/`PRINTFORMLC`）、`:277-306`（`K` 系：`PRINTK`…`PRINTFORMSKW` 15 个在 `:277-295`、定宽 K 4 个在 `:297-300`、`PRINTSINGLE` 的 K 5 个在 `:302-306`）、`:312-341`（`D` 系：15 个在 `:312-330`、定宽 D 4 个在 `:332-335`、`PRINTSINGLE` 的 D 5 个在 `:337-341`）、`:398-402`（`N` 系 5 个，位于 `#region Emuera.NET`；`:308-310` 的 `PRINTDATAK` 系与 `:343-345` 的 `PRINTDATAD` 系则属 PRINTDATA 族，不归本文件）
- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:70-73`（`addPrintFunction` → `new PRINT_Instruction(code.ToString())`）；调用点 `:84-133`（基础/`K`/`D` 三组 45 个）、`:150-161`（C/LC 4 个）、`:445-449`（`N` 系 5 个，`#region Emuera.NET`）
- 标志常量：`Runtime/Script/Statements/FunctionIdentifier.cs:29-37`（`PRINT_NEWLINE`／`PRINT_WAITINPUT`／`ISPRINTDFUNC`／`ISPRINTKFUNC`／`IS_PRINT`）；判定函数 `:579-601`（`IsPrintDFunction`／`IsPrintKFunction`／`IsNewLine`／`IsWaitInput`／`IsPrintSingle`）
- 实现类：`Runtime/Script/Statements/Instraction.Child.cs:84-223`（`PRINT_Instruction`；后缀解析 `:87-174`，执行 `:180-222`）
- 输出通路：`Runtime/Script/Statements/ExpressionMediator.cs:47-62`（`OutputToConsole`）、`UI/Game/EmueraConsole.Print.cs:455-476`（`Print`）、`:525-531`（`PrintC`）、`:547-594`（`CreateTypeCString`）、`:628-632`（`NewLine`）、`UI/Game/PrintStringBuffer.cs:62-87`（`Append`，`isLastLineEnd`）、`:159-175`（`FlushSingleLine`／`Flush`）
- 参数构造器：`Runtime/Script/Statements/ArgumentBuilder.cs:499-518`（`SP_PRINTV`）、`:614-658`（`STR_NULLABLE`）、`:660-700`（`FORM_STR_NULLABLE`）、`:1451-1476`（`STR_EXPRESSION`）；参数类型枚举 `Runtime/Script/Statements/FunctionArgType.cs:14/18/20/21`；参数对象 `Runtime/Script/Statements/Argument.cs:115-122`（`ExpressionArgument`）、`:134-141`（`SpPrintVArgument`）
- 执行入口与 SKIPDISP：`Runtime/Script/Process.ScriptProc.cs:19-90`（主循环，`:46-55` 通用 skipPrint 判定）、`:566-572`（`FORCEKANA`）、`:573-580`（`SKIPDISP`）

```text
# ───────────── 一、构造期：后缀解析（Instraction.Child.cs:87-174） ─────────────
构造 PRINT_Instruction(name):                       # name = 枚举成员名，如 "PRINTSDW"
    flag = IS_PRINT                                 # :93  所有 PRINT 族都受 SKIPDISP 影响
    st = CharStream(name); st.Jump(5)               # :94-95 跳过 "PRINT"
    if st.CurrentEqualTo("SINGLE"):                 # :96-100
        flag |= PRINT_SINGLE | EXTENDED; st.Jump(6)
    # ── 值类型选择器（顺序 V → S → FORMS → FORM → 其余）──
    if   st.CurrentEqualTo("V"):     ArgBuilder = SP_PRINTV;      isPrintV = true;  st.Jump(1)   # :102-107
    elif st.CurrentEqualTo("S"):     ArgBuilder = STR_EXPRESSION;                  st.Jump(1)   # :108-112
    elif st.CurrentEqualTo("FORMS"): ArgBuilder = STR_EXPRESSION; isForms  = true; st.Jump(5)   # :113-118
    elif st.CurrentEqualTo("FORM"):  ArgBuilder = FORM_STR_NULLABLE;               st.Jump(4)   # :119-123
    else:                            ArgBuilder = STR_NULLABLE    # 无选择器 = <文本>       # :124-127
    # ── 对齐选择器（LC 先于 C）──
    if   st.CurrentEqualTo("LC"): flag |= EXTENDED; isLC = true; st.Jump(2)                      # :128-133
    elif st.CurrentEqualTo("C"):  if name == "PRINTFORMC": flag |= EXTENDED
                                  isC = true; st.Jump(1)                                          # :134-140
    # ── 配色选择器 ──
    if st.CurrentEqualTo("K"): flag |= ISPRINTKFUNC | EXTENDED; st.Jump(1)                       # :141-145
    if st.CurrentEqualTo("D"): flag |= ISPRINTDFUNC | EXTENDED; st.Jump(1)                       # :146-150
    # ── N（不换行等待）──
    if st.CurrentEqualTo("N"): isLineEnd = false; flag |= PRINT_WAITINPUT; st.Jump(1)             # :151-156
    # ── 行尾选择器 ──
    if   st.CurrentEqualTo("L"): flag |= PRINT_NEWLINE;  flag |= METHOD_SAFE; st.Jump(1)          # :157-162
    elif st.CurrentEqualTo("W"): flag |= PRINT_NEWLINE | PRINT_WAITINPUT                          # :163-167
    else:                        flag |= METHOD_SAFE     # 无后缀；N 也落到这里 → 意外 METHOD_SAFE # :168-171
    if ArgBuilder == null or not st.EOS:                                                          # :172-173
        throw ExeEE("PRINT異常")                     # 后缀没吃干净/无法识别

# ───────────── 二、执行期：分发（Process.ScriptProc.cs:34-61） ─────────────
runScriptProc 每取一行 InstructionLine func:
    if func.Argument == null: 解析参数; 若 func.IsError → throw CodeEE(func.ErrMes)               # :40-45
    if skipPrint and func.Function.IsPrint():       # SKIPDISP 生效（:46-55）
        if userDefinedSkip and func.Function.IsInput(): 报错并终止执行（:48-53）
        continue                                    # 整条命令（含换行/等待）被跳过
    func.Function.Instruction.DoInstruction(exm, func, state)    # PRINT 族走这里（:56-57）

# ───────────── 三、执行期：统一输出流程（Instraction.Child.cs:180-222） ─────────────
DoInstruction(PRINT 族):
    if GlobalStatic.Process.SkipPrint: return                                        # :182-183
    Console.UseUserStyle    = true                                                   # :184
    Console.UseSetColorStyle = not IsPrintDFunction()     # D 后缀 → false（用默认色） # :185
    # 1) 取字符串
    if func.Argument.IsConst:            str = func.Argument.ConstStr                # :187-188
    elif isPrintV:                       # V：逐项拼接（数值→十进制，字符串→原样）  # :189-201
        for term in SpPrintVArgument.Terms:
            if term.OperandType == long: builder.Append(term.GetIntValue(exm))
            else:                        builder.Append(term.GetStrValue(exm))
        str = builder.ToString()
    else:
        str = ((ExpressionArgument)func.Argument).Term.GetStrValue(exm)               # :204
        if isForms:                       # FORMS：对求值结果做转义后按 FORM 再解析     # :205-211
            str = CheckEscape(str)
            str = StrForm.FromWordToken(AnalyseFormattedString(str)).GetString(exm)
    # 2) 假名转换（K 后缀）
    if IsPrintKFunction(): str = exm.ConvertStringType(str)                          # :213-214
    # 3) 输出
    if   isC:  Console.PrintC(str, alignmentRight: true)      # 填充后右对齐           # :215-216
    elif isLC: Console.PrintC(str, alignmentRight: false)     # 填充后左对齐           # :217-218
    else:      exm.OutputToConsole(str, func.Function, isLineEnd)                    # :219-220
    Console.UseSetColorStyle = true    # 还原，避免 D 影响后续输出                    # :221

# ───────────── 四、输出通路细节 ─────────────
OutputToConsole(str, func, lineEnd):                       # ExpressionMediator.cs:47-62
    if func.IsPrintSingle():
        Console.PrintSingleLine(str, temporary: false)      # 见 PRINTSINGLE.md
    else:
        Console.Print(str, lineEnd)                         # Print 会在 '\n' 处递归拆分
        if func.IsNewLine() or func.IsWaitInput():
            Console.NewLine()                               # 强制 flush 成显示行
            if func.IsWaitInput(): Console.ReadAnyKey()      # 等待回车
    Console.UseSetColorStyle = true

Console.Print(str, lineEnd):                     # EmueraConsole.Print.cs:455-476
    if str 为空: return
    if str 含 '\n': printBuffer.Append(前半段)（默认 lineEnd=true）+ NewLine() + 递归 Print(后半段)
    else: printBuffer.Append(str, Style, lineEnd=lineEnd)
        # Append 会记录 isLastLineEnd = lineEnd（PrintStringBuffer.cs:62-64）
        # 文本留在缓冲里，直到 NewLine() → PrintFlush(true) 时才成为显示行（:628-632）

Console.PrintC(str, alignmentRight):              # EmueraConsole.Print.cs:525-531
    if str 为空: return
    printBuffer.Append(CreateTypeCString(str, alignmentRight), Style, lineEnd: true)
    # CreateTypeCString（:547-594）：按 Shift-JIS 字节长度补/删半角空格至 PrintCLength（默认 25）

# ───────────── 五、N 后缀的特殊行为 ─────────────
# 构造期：isLineEnd=false 且 PRINT_WAITINPUT（:151-156）
# 执行期：Console.Print(str, lineEnd:false)
#   → PrintStringBuffer.isLastLineEnd = false（PrintStringBuffer.cs:64）
#   → IsWaitInput() 为真 → NewLine() 强制 flush，flush 出的行 IsLineEnd=false（:172）
#   → ReadAnyKey() 等待回车
# 后续第一次 addDisplayLine 时（EmueraConsole.Print.cs:217-224）：
if displayLineList.Count != 0 and not displayLineList[^1].IsLineEnd:
    deleteLine(1)                                    # 删掉那一行
    newLine.ShiftPositionX(上一行末尾的 X)            # 新内容接着画
    newLine.ChangeStr(上一行内容 + 新内容)            # 合并成同一显示行
```

## 备注

- **文档与源码的差异**
  - ecd 文档第 3 个括号的说明只写「`W` - 输出文本后执行 WAIT」，源码是**先换行再等待**（`PRINT_NEWLINE | PRINT_WAITINPUT`），与同页「PRINT系列指令辅助选择器」的「换行等待：无换行无等待 / 只换行 / 换行后等待」一致；以源码为准。
  - ecd 文档说「从 1.736 版本开始，关键词 `K` 和 `D` 不能同时指定」。源码里解析器是顺序匹配（先 K 后 D，见 `Runtime/Script/Statements/Instraction.Child.cs:141-150`），真正的限制来自**枚举名集合中没有同时含 K、D 的成员**，因此任何 `PRINTKD` 之类的写法都不存在、会被当成未定义命令。
  - zh 文档（Era-Chinese-Documentation）该节把第 3 个括号的三项误写作「`无`：只打印，无换行或等待 / `K`：`Print` 后换行 / `D`：`Print` 之后换行并执行 `Wait`」——关键词应当是 `L` 与 `W`（对照 ecd 的「无 / L / W」），属旧版排版错误；括号内描述本身与 ecd 一致。
  - 两套文档都**没有逐个收录**这 54 个后缀变体名（只给出 `PRINT(|V|S|FORM|FORMS)(|K|D)(|L|W)` 这样的模板），权威清单 `emuera_standard_cmds.txt` 同样不含这些名字——这就是需要本基名文档的原因。
- **实现怪癖**
  - `N` 系虽会等待输入，却因后缀匹配顺序落进 `else → METHOD_SAFE`（`Runtime/Script/Statements/Instraction.Child.cs:168-171`），于是可以在 `#FUNCTION` 中使用；`W` 系则不行。二者行为上的「等待」是一样的。
  - `PRINTV`（以及 `PRINTVL` 等 V 系）**允许一个参数都不写**（`Runtime/Script/Statements/ArgumentBuilder.cs:503-516`：空参数表不产生警告），此时输出空串；因此 `PRINTVL` 单独一行等价于「只换行」。
  - `PRINTN` 中若字符串本身含 `\n`，`Console.Print` 的递归拆分会让**尾部片段按 `lineEnd = true`（默认值）追加**（`UI/Game/EmueraConsole.Print.cs:466-470` 的 `Print(lower)` 未传递 lineEnd），"非行尾"标记只在首段生效——属边缘情形。
  - `PRINTSINGLE` 族在构造函数里也被打上 `EXTENDED` 位（`:96-100`）；定宽系中 `PRINTFORMC`（`:136-137`）与 `PRINTLC`／`PRINTFORMLC`（`LC` 分支无条件置位，`:128-133`）同样带 `EXTENDED`，只有 `PRINTC` 不打。
- **生态差异**：本仓库另有 C++/Qt 移植 `src/eraengine`，其 PRINT 族后缀解析在 `GameData/ast/ast_builder.cpp:146-232`（`AstBuilder::printInfo`）复刻了同一套顺序，但**字符集里不含 `N`**（`:183` 的 `"VSLWCKDFORM"`），即该移植尚未支持 `PRINTN`/`PRINTVN`/`PRINTSN`/`PRINTFORMN`/`PRINTFORMSN` 这 5 个成员。
- **相关文档**：`PRINTSINGLE.md`（另 15 个成员）、`PRINTPLAIN.md`、`PRINTBUTTON.md`、`PRINTDATA.md`、`PRINTCPERLINE.md`、`PRINTCK.md`/`PRINTCD.md`/`PRINTFORMCK.md`/`PRINTFORMCD.md`/`PRINTFORMLCK.md`/`PRINTFORMLCD.md`/`PRINTLCK.md`/`PRINTLCD.md`（EE 定宽变体）。
