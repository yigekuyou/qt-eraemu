# PRINTSINGLE

- **类别**：命令（Emuera 内建，非 EE 扩展——尽管构造时被打了 `EXTENDED` 位；共 15 个枚举成员由本文件统一覆盖）
- **签名**：`PRINTSINGLE(|V|S|FORM|FORMS)(|K|D) <参数>`
  - 无值类型选择器：`PRINTSINGLE <文本>`（行剩余部分整体作为字面文本）
  - `V`：`PRINTSINGLEV <数值表达式>[, <数值表达式> …]`
  - `S`：`PRINTSINGLES <字符串表达式>`
  - `FORM`：`PRINTSINGLEFORM <FORM格式文本>`
  - `FORMS`：`PRINTSINGLEFORMS <FORM格式字符串表达式>`
  - 配色：`K`＝应用 `FORCEKANA`；`D`＝忽略 `SETCOLOR` 用默认色（语义与 PRINT 族完全相同）
  - **没有 `L`/`W`/`C`/`LC` 后缀**：输出总是独占一个显示行（详见下）
  - 15 个可写名字：`PRINTSINGLE`、`PRINTSINGLEV`、`PRINTSINGLES`、`PRINTSINGLEFORM`、`PRINTSINGLEFORMS`、`PRINTSINGLEK`、`PRINTSINGLEVK`、`PRINTSINGLESK`、`PRINTSINGLEFORMK`、`PRINTSINGLEFORMSK`、`PRINTSINGLED`、`PRINTSINGLEVD`、`PRINTSINGLESD`、`PRINTSINGLEFORMD`、`PRINTSINGLEFORMSD`
- **文档来源**：`ecd/docs/translation/Command.md`「### PRINTSINGLE(|V|S|FORM|FORMS)(|K|D)」「### PRINT系列指令辅助选择器」（「单行溢出：溢出后自动换行 / **溢出后不换行**」）；`Era-Chinese-Documentation/docs/translation/Command.html`（提取本 `_extracted/zh/Command.md`）「### # PrintSingle(|V|S|Form|FormS)(|K|D)」。两套文档的命令总表只以 `PRINTSINGLE` 一条基名列出（`ecd/docs/translation/ERB_Commands.md:132`）；权威清单 `test/data/emuera_standard_cmds.txt` 已逐个收录这 15 个变体名，本文件把它们按基名统一说明（语义以源码为准）

## 语义

`PRINTSINGLE` 是 `PRINTL` 的**不折行版**：文本以「一个显示行」为单位整条输出，**超出窗口宽度的部分不会被折到下一行，也不会被绘制**（ecd：「PRINTSINGLE系列指令与 `PRINTL` 指令大致相同，不同点在于 `PRINTSINGLE` 绘制文本超出画面宽度时不会自动换行。超出画面的文字不会被绘制。因为在输出后会换行，所以没有 `(|L|W)` 关键字。其他关键字与 PRINT 的关键字意义相同。」）。

具体机制（源码）：

- 输出走**独立通路**，不经过 `Console.Print`：`ExpressionMediator.OutputToConsole` 检测到 `PRINT_SINGLE` 标志后调用 `Console.PrintSingleLine(str, temporary: false)`（`Runtime/Script/Statements/ExpressionMediator.cs:47-51`），**因此 PRINT 那套「按 `\n` 拆成多行 + 按 `IsWaitInput`/`IsNewLine` 追加换行与等待」的逻辑对 PRINTSINGLE 完全不生效**。
- `PrintSingleLine` 做三件事（`UI/Game/EmueraConsole.Print.cs:441-453`）：
  1. 空字符串（null 或 `""`）直接返回——**不产生任何行**（与 `PRINTL` 空参仍换行相反）；
  2. 先 `PrintFlush(false)`，把此前遗留的未定型缓冲（例如上一句不带 `L` 的 `PRINT` 文本）定型成普通行；
  3. 把本行文本作为一个**不折行**的显示行加入（`BufferToSingleLine(force: true, temporary: false)` → `FlushSingleLine` 内部以 `setWidthToButtonList(..., nobr: true)` + 单条 `ConsoleDisplayLine` 构造，折行判定在 `UI/Game/PrintStringBuffer.cs:192/211` 被 `nobr` 短路）。
- 「输出后换行」的实际含义是「本行到此为止，下一句输出另起一行」：因为整行被标记为 `IsLineEnd = true`（`ConsoleDisplayLine` 构造默认值，`UI/Game/ConsoleDisplayLine.cs:42/49`），后续输出不会与它拼接，而 `temporary: false` 意味着它是**普通行**，不会被下一次输出替换（对比 `REUSELASTLINE`：同一个单行原语、但 `temporary: true`，见 `Runtime/Script/Statements/Instraction.Child.cs:661-674` 与 `UI/Game/EmueraConsole.Print.cs:173` 的「上一行是临时行则先删除」）。
- 参数类型选择器（`V`/`S`/`FORM`/`FORMS`）与配色选择器（`K`/`D`）的语义、参数构造器、执行时的取字符串逻辑**与 PRINT 族逐一相同**（同一份 `PRINT_Instruction` 代码），详见 `PRINT.md`。
- 其他共性：受 `SKIPDISP` 影响（`Runtime/Script/Statements/Instraction.Child.cs:182-183`）；`K` 走 `FORCEKANA` 假名转换（`:213-214`）；字体样式取 `SETCOLOR` 或 `D` 后缀指定的默认色（`:184-185`）；`ALIGNMENT` 设置照常生效；`[数字] 文本` 照常自动按钮化（按钮在 `FlushSingleLine` 里由 `fromCssToButton()` 生成）；单行缓冲超 2000 字照常截断并显示溢出提示（`UI/Game/PrintStringBuffer.cs:65-74`）。
- 15 个成员全部带 `METHOD_SAFE`（后缀链最后落到 `else` 分支，`Runtime/Script/Statements/Instraction.Child.cs:168-171`），因此**可以在 `#FUNCTION` 内使用**；由于没有 `W`/`N` 后缀，它从不等待输入，也就不受「`W` 系不可在 `#FUNCTION` 中使用」的限制。

## 用法

### `PRINTSINGLE <文本>` / `PRINTSINGLEFORM <FORM格式文本>`

```erb
PRINTSINGLE 这一行太长时不会被折到下一行，超出窗口宽度的部分不显示
PRINTSINGLEFORM 进度：{LOCAL:0}／100
```

### `PRINTSINGLEV <数值表达式>[,…]` / `PRINTSINGLES <字符串表达式>`

```erb
PRINTSINGLEV RESULT
PRINTSINGLES "状态：" + NAME:MASTER
```

### `PRINTSINGLEFORMS <FORM格式字符串表达式>`

先求值成字符串，再当 FORM 文本二次展开：

```erb
S = "第 %LOCAL:0% 回合：{LOCAL:1} 点"
PRINTSINGLEFORMS S
```

### 配色后缀

```erb
PRINTSINGLEK 这段文本会按 FORCEKANA 的设置做假名/半角转换
PRINTSINGLED 这段文本用设置文件里的默认字色（忽略 SETCOLOR）
```

## 源码实现（emuera.em/Emuera）

- 枚举：`Runtime/Script/Statements/BuiltInFunctionCode.cs:125-129`（`PRINTSINGLE`／`V`／`S`／`FORM`／`FORMS`）、`:302-306`（`K` 系 5 个）、`:337-341`（`D` 系 5 个）
- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:134-148`（15 个 `addPrintFunction(...)`，均落到 `addPrintFunction`→`new PRINT_Instruction(code.ToString())`，`:70-73`）
- 实现类（与 PRINT 族共用）：`Runtime/Script/Statements/Instraction.Child.cs:84-223`（`PRINT_Instruction`）；`SINGLE` 识别与标志置位 `:96-100`
- 特有分支：`Runtime/Script/Statements/ExpressionMediator.cs:47-51`（`OutputToConsole` 中 `IsPrintSingle()` → `Console.PrintSingleLine(str, false)`）
- 控制台：`UI/Game/EmueraConsole.Print.cs:441-453`（`PrintSingleLine`）、`:634-644`（`BufferToSingleLine`）、`:166-252`（`addDisplayLine`：先删临时行 `:173`，再做非行尾合并 `:217-224`）、`:628-632`（`NewLine`）；`UI/Game/PrintStringBuffer.cs:159-166`（`FlushSingleLine`）、`:187-285`（`ButtonsToDisplayLines`，`nobr` 短路在 `:211`）；`UI/Game/ConsoleDisplayLine.cs:42/49`（`IsLineEnd`）
- 对照实现：`REUSELASTLINE_Instruction`（`Runtime/Script/Statements/Instraction.Child.cs:661-674`，`PrintTemporaryLine` → `PrintSingleLine(str, temporary: true)`，`UI/Game/EmueraConsole.Print.cs:334-337`）

```text
# ───────────── 一、构造期（Instraction.Child.cs:87-174）中的 SINGLE 分支 ─────────────
构造 PRINT_Instruction(name):                       # name 形如 "PRINTSINGLEFORMD"
    flag = IS_PRINT                                 # :93
    st = CharStream(name); st.Jump(5)               # :94-95 跳过 "PRINT"
    if st.CurrentEqualTo("SINGLE"):                 # :96
        flag |= PRINT_SINGLE | EXTENDED             # :98   ← PRINTSINGLE 族唯一的身份标志
        st.Jump(6)                                  # :99
    # 之后与 PRINT 族共用同一段后缀解析（:102-171）：
    #   值类型 V / S / FORMS / FORM / 其余 → 决定参数构造器
    #   K → ISPRINTKFUNC | EXTENDED；D → ISPRINTDFUNC | EXTENDED
    #   L / W 分支在本族中永不命中（枚举名里没有 PRINTSINGLEL / PRINTSINGLEW）
    #   于是落到 else → flag |= METHOD_SAFE        # :168-171
    if ArgBuilder == null or not st.EOS:
        throw ExeEE("PRINT異常")                     # :172-173

# ───────────── 二、执行期（Instraction.Child.cs:180-222，本族专有的分叉在第三步） ─────────────
DoInstruction:
    if GlobalStatic.Process.SkipPrint: return        # :182-183  （SKIPDISP 生效）
    Console.UseUserStyle = true                      # :184
    Console.UseSetColorStyle = not IsPrintDFunction()  # :185   D 后缀 → 用默认色
    # 取字符串：与 PRINT 族完全相同
    if Argument.IsConst:      str = Argument.ConstStr                  # :187-188
    elif isPrintV:            拼接 SpPrintVArgument.Terms（数值/字符串分别取值）# :189-201
    else:
        str = ((ExpressionArgument)Argument).Term.GetStrValue(exm)      # :204
        if isForms: str = CheckEscape(str) → 按 FORM 解析 → GetString(exm)  # :205-211
    if IsPrintKFunction(): str = exm.ConvertStringType(str)            # :213-214
    # → 本族没有 C/LC 分支，直接走第三条路的 OutputToConsole（:219-220）
    exm.OutputToConsole(str, func.Function, isLineEnd)                 # :219-220

# ───────────── 三、单行通路（本族的关键差异） ─────────────
OutputToConsole(str, func, lineEnd):                 # ExpressionMediator.cs:47-62
    if func.IsPrintSingle():                         # :49   PRINT_SINGLE 标志
        Console.PrintSingleLine(str, temporary: false)   # :50
        # 注意：不执行 Console.Print / 不执行 NewLine() / 不执行 ReadAnyKey()
        #       isLineEnd（N 后缀才置 false）在这里被完全忽略
    else: …（PRINT 族的分支，见 PRINT.md）
    Console.UseSetColorStyle = true                  # :61

Console.PrintSingleLine(str, temporary = false):     # EmueraConsole.Print.cs:441-453
    if string.IsNullOrEmpty(str): return             # :444-445  空串 → 一行都不产生
    PrintFlush(false)                                # :446  先把遗留的未定型缓冲定型成普通行
    printBuffer.Append(str, Style)                   # :447  整串追加（不做 '\n' 拆分）
    dispLine = BufferToSingleLine(force: true, temporary: false)          # :448 → :634-644
                  → FlushSingleLine(stringMeasure, temporary):            # PrintStringBuffer.cs:159-166
                        fromCssToButton()               # 自动按钮化（[数字] 文本）
                        setWidthToButtonList(m_buttonList, sm, nobr: true)  # :162
                        #   nobr=true → ButtonsToDisplayLines 的折行判定被短路（:211）
                        #   整行只生成 1 条 ConsoleDisplayLine（:163），溢出部分在绘制时被裁掉
    addDisplayLine(dispLine, force_LEFT: false)      # :451 → :166-252
    if LastLineIsTemporary: deleteLine(1)            # :173  只删「临时行」；本族 temporary=false，不受影响
    #   随后还有一处合并判定（:217-224）：若上一行 IsLineEnd==false（PRINTN 的产物）
    #   就把上一行删掉、把本行内容接在它后面画；本族产出的行 IsLineEnd==true，不会与之合并
    RefreshStrings(false)                            # :452

# ───────────── 四、与 REUSELASTLINE 的对照（同一原语，temporary 不同） ─────────────
REUSELASTLINE <FORM文本>:                             # Instraction.Child.cs:661-674
    str = 求值 FORM 文本
    Console.PrintTemporaryLine(str)                  # :672 → EmueraConsole.Print.cs:334-337
        → PrintSingleLine(str, temporary: true)      # 同样的单行显示，但标记为「临时行」
        → 下一次 addDisplayLine 时被 deleteLine(1) 删除（:173），
          或下一次 PrintSingleLine 时被覆盖 —— 这就是「重写最后一行」的机制
PRINTSINGLE:
    → PrintSingleLine(str, temporary: false)         # 普通行，不会被后续输出替换，
                                                     # 只能靠 CLEARLINE 等命令删除
```

## 备注

- **与 PRINT 族（`PRINTL`）的差别**（小结）：
  | 项目 | `PRINTL` | `PRINTSINGLE` |
  |---|---|---|
  | 折行 | 超出窗口宽度自动折到下一显示行 | **不折行**，超出部分不绘制（整行一条 `ConsoleDisplayLine`，`nobr: true`） |
  | 后缀 | 有 `L`/`W`，以及 `K`/`D` | 只有 `K`/`D`（输出后总是「换行」，故无 `L`/`W`） |
  | 内嵌 `\n` | `Console.Print` 按 `\n` 递归拆成多行（`UI/Game/EmueraConsole.Print.cs:460-472`） | 不拆分，整串一起交给单行显示（`PrintSingleLine` 直接 `Append`） |
  | 空参数/空串 | 仍产生一个空行 | **不产生任何行**（`:444-445` 早退） |
  | 遗留缓冲 | 不处理 | 先把遗留缓冲定型成普通行（`:446`） |
  | 行性质 | 普通行 | 普通行（与 `REUSELASTLINE` 的「临时行」不同） |
- **ecd 文档的两点对齐/偏差**：
  - 「超出画面的文字不会被绘制」与源码一致（整行 `nobr` 单行，绘制时按窗口宽度裁剪）；文档所说「因为在输出后会换行」在源码里体现为「每一次 PRINTSINGLE 都独立成行、`IsLineEnd = true`」，而非调用 `NewLine()`。
  - 文档写的是 `PRINTSINGLE(|V|S|FORM|FORMS)(|K|D)`，与枚举名集合完全一致（无 `L`/`W`/`C`/`LC` 变体）。
- **两套文档都没有逐个收录**这 15 个后缀变体名（只给出模板形式），权威清单 `emuera_standard_cmds.txt` 亦然——这是需要本基名文档的原因。
- **实现细节**：`PRINTSINGLE` 在构造函数里被打了 `EXTENDED` 位（`Runtime/Script/Statements/Instraction.Child.cs:96-100`），但 ecd 与 zh 两套文档都把它当作标准命令收录，不属 EE 私有扩展。
- **相关文档**：`PRINT.md`（PRINT 族 54 个成员与统一执行流程）、`PRINTPLAIN.md`、`PRINTBUTTON.md`、`PRINTDATA.md`、`PRINTCPERLINE.md`。
