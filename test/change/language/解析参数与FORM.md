# 参数解析与 FORM 修复（2026-10-08）

C# 语义依据：`test/data/commands/{CUSTOMDRAWLINE,CASE,TIMES,ENCODETOUNI}.md`、
`test/data/language/表达式.md` 及对应参考源码。以下记录 C++ 移植改动。

- `CUSTOMDRAWLINE` 按单个原始文本参数处理，保留逗号、引号和尾部空白，空参数产生诊断。
- `CASE` 按值、闭区间 `TO`、`IS` 比较条件解析；装载期完成声明处理后重建缓存，执行期只消费 AST。
- FORM、普通字符串及插值跨度统一用于表达式词法、参数分割和 CASE 扫描；条件内引号、逗号、`TO` 不再提前截断外层内容。
- 条件 FORM 的分支按文本读取分隔符，保留全角空格，仅裁剪 ASCII 空格和制表符。
- `TIMES` 倍率按有限实数常量校验，不再交给整数表达式解析；写回规则见下方 2026-10-10 更新，当前采用可配置取整，不代表已实现 C# 配置控制的 decimal 精度分支。
- CALL 支持函数名与括号参数间的空白；CALLFORM 目标中的插值不再被内部空白、逗号或括号切断。
- ENCODETOUNI 命令与同名式中函数分流：命令展开 FORM 并写入 RESULT 数组，式中函数仍接收普通字符串表达式。
- AST 磁盘缓存已移除；告警导出工具的 `--async` 仍用于验证并行装载。

## 验证记录

Megaten 完整装载的解析警告由 3838 条降为 0。同步装载与异步装载
（8396 个文件）均为 0；重新构建的 test_cli 装载 8344 个脚本后同样报告 0 警告。
测试覆盖原始警告、CASE 边界、FORM 真/假分支与字符串下标、特殊参数、CALL 分割和
ENCODETOUNI 命令/函数执行区别。

扩大验证共 39 项，38 项通过；唯一失败为工具传输测试的 stderr 空断言，Debug 子进程输出
QML debugging 提示。此记录不代表验证了完整游戏交互或所有 C# 兼容行为。

## 补充：SP_GETINT 变量槽指令（2026-10-09）

C# 语义依据：`test/data/commands/{SAVENOS,PRINTCPERLINE}.md`、
`test/data/functions/{SAVENOS,PRINTCPERLINE}.md`。

- `SAVENOS` / `PRINTCPERLINE` 的**语句形式**（`SAVENOS <数值变量>`）在 C# 里注册为
  `argb[FunctionArgType.SP_GETINT]`：实参是**可赋值变量**，语义是「把
  `Config.SaveDataNos` / `Config.PrintCPerLine` 写进该变量」。本移植此前把整行按
  式中函数归约（`SAVENOS()`，0 参）：`SAVENOS L_MAX` 报
  「参数过多（最多 0 个，实得 1）」，且对变量的赋值被丢弃
  （真实游戏 `SYSTEM_DATA_FUNC.ERB:49/197` 即此）。
  修复：`argument_parser.h` 登记 `ArgKind::GetInt`（1..1），`ast_builder.cpp` 的
  「内置函数名开头 → 函数语句」例外名单加上这两个名字（与 `POWER` / `ENCODETOUNI`
  同形），执行链新增写变量分支。
- `SAVENOS()`（式中函数）此前被实现成「把 NOS 数组写入 `sav/nos.dat` 并返回 1」；
  按 C# `GetSaveNosMethod` 改为返回 `Config.SaveDataNos`，不再有文件副作用
  （参考树全树无 `nos.dat`）。`SystemHost::saveDataNos` 一并改为复用同一 provider ——
  它此前查的键名 `セーブデータの数` 与 C# 的 `表示するセーブデータ数` 对不上，恒取默认值。
- 回归：`test_argument_types`（语句按指令解析 / 个数 / 首参须为变量）、
  `test_statements` §8（执行写到变量）、`test_builtin_functions`（`SAVENOS()` 返回值）。

## 补充：新游戏开局的初始角色（2026-10-09）

C# 语义依据：`test/data/language/内置流程.md`（`endOpenning`）、`test/data/language/CSV文件.md`
（`最初からいるキャラ`）、`test/data/commands/{ADDCHARA,ADDDEFCHARA,DELCHARA}.md`。

- `EraEngine` 的 `SystemHost` 此前只接了 `resetData`，**未接线**
  `addCharacterFromCsvNo` / `defaultCharacter` —— 状态机里的
  `if (m_host.addCharacterFromCsvNo)` 恒为假，于是「[0] 最初始开始」后一个角色都没登记
  （`CHARANUM == 0`），脚本惯例的 `DELCHARA 0`（删掉开局那个 0 号角色）直接报
  「DELCHARA 的番号超出角色范围: 0」（真实汉化游戏 `SYSTEM.ERB:58` 即此）。
  修复：按 C# `endOpenning`（`ResetData()` → `AddCharacterFromCsvNo(0)` →
  `DefaultCharacter > 0` 时再加）接上这两个回调。
- `GameBaseData` 此前只建模 6 个显示用键，其余 `GameBase.csv` 键**直接丢弃**，
  于是 `get("最初からいるキャラ")` 恒空 —— 新局默认角色与 `ADDDEFCHARA`
  （`execution_engine.cpp` 读同一键）都因此失效。修复：未建模的键原样留存，`get()` 回退查它。
- 回归：`test_new_game`（自建「无 `@SYSTEM_TITLE`」小游戏 → `runSystem()` →
  `chooseTitle(0)`，断言 `CHARANUM` 与 `charaCsvNo`）。

## 浮点倍率与三种取整方式（2026-10-10）

- TIMES 的第二参数属于命令 AST 实数常量，普通整数表达式仍统一为 qint64。
  装载期以 QDoubleValidator 校验（C locale、拒绝分组符、ScientificNotation、
  不限制小数位，只接受 Acceptable），用同一 locale 转为有限 double 后存入
  Operand::realValue。参数类型回填复用解析结果，执行期间不再从 raw 转换。
- Qt 扩展配置 `RealRounding`：`round`（默认）、`floor`、`ceil`。
  QML 设置通过可翻译的 ComboBox 编辑，复用 ConfigLoader 的配置保存路径。
  `round` 使用 qRound64，半值远离零；floor/ceil 使用 std::floor/std::ceil，
  因为 Qt qFloor/qCeil 的返回类型是 int，无法覆盖 qint64 范围。
- 三种方式均在整数转换前检查有限性和 qint64 边界。溢出报告执行错误，保留目标值。
- 有意移植差异：C# TIMES 的 `(long)d` 向零截断，本移植默认四舍五入。
  `10 * 0.75` 在 C# 中为 7，在默认 Qt 模式下为 8；`-10 * 0.75` 为 -8。
  C# double/decimal 配置分支仍未完整移植，旧配置不覆盖新增取整选项。
- Qt 官方依据：QDoubleValidator、QtNumeric::qRound64、QtMath::qFloor/qCeil、
  Qt Quick Controls ComboBox（textRole/valueRole/currentValue/onActivated）。
- 回归覆盖科学计数法、小数缓存、非法/非有限倍率、三种正负数取整、
  超出 int 范围的结果和乘法溢出。

## 字符串装载期 AST（2026-10-10）

- 普通字符串参数在装载期创建 LiteralNode；`@"..."` 和 `\@...\@`
  经表达式词法的 FORM token 去除定界符，FORM provider 再生成文本、插值、
  条件分支等 StrForm AST。普通引号里的 `%`/`{}` 不自动变成 FORM，整数取模仍为整数表达式。
- FORM 条件里的用户函数在首次装载时允许 Unknown 返回类型，后续函数类型回填与
  validateExpression 继续检查 Int 约束，避免把尚未登记的用户函数当作格式错误。
- 已知字符串左值的 `=` 立即生成原始 FORM AST，其余用户声明在 finalizeParse
  的 applyStringAssignments 中完成；保留 `=` 的引号文本语义以及 `'=` 的字符串表达式语义。
- 字符串赋值、REUSELASTLINE、DRAWLINEFORM、DEBUGPRINT、SPLIT、按钮、
  存档文字、HTML_TAGSPLIT 等执行入口使用已装载的 AST；移除这些入口从 raw
  重新解析字符串的回退。STRLENFORM 不再对已经求出的文本二次展开。
- PRINTFORMS 已按后续要求改为装载期静态分片，见下方更新；显式运行期表达式接口独立保留。
- 回归：FORM 定界符去除、插值/条件文本、普通字符串 Literal AST、整数取模、
  原始 FORM 赋值引号保留；完整构建与 42/42 测试通过。
- Megaten 真实装载检查：8344 脚本、14514 变量、352 条诊断（原日志 374）。
  FORM 无法归约的误报消失，剩余 expr-parse 1 条为 `CFLAG:800 & 1p63`；
  arg-check 347 条、numeric-conversion 4 条仍需分别处理，未宣称全部告警清零。

## PRINTFORMS 静态片段（2026-10-10）

- PRINTFORMS 的静态字符串字面量在装载期由 StrFormParser 分词，文本、
  `%变量%`、`{表达式}` 和条件分支保存在 AST；字符串拼接编译为有序片段。
- 裸字符串变量或函数调用作为独立表达式运算节点，执行时读取当前值。
  读取结果作为文本拼接，不重新词法分析，也不把结果中的格式标记二次展开。
- 移除 handlePrint 中运行期 StrFormParser::parse(text) 分支。
  这是对 C# PRINTFORMS 动态二次展开语义的有意调整。
- 示例：`PRINTFORMS "score={LOCAL}; " + LOCALS`；LOCAL=42、
  LOCALS=`value {LOCAL}` 时输出 `score=42; value {LOCAL}`。
- 回归覆盖静态字面量插值、字符串拼接、独立变量读取、变量当前值变化、
  原始参数文本修改不影响 AST、变量值中的格式标记保持文本；42/42 测试通过。

## 剩余字符串执行入口迁移（2026-10-10）

- CALLFORM/CALLFORMF 与 TRY 变体的目标名在装载期保存 FORM AST，运行期只求值目标。
  RETURNFORM 直接求值已编译参数，不再从 raw 重建 FORM。
- CALLF 使用已有参数 AST 构造调用节点，移除运行期拼接调用源码、补引号并重新分词的路径。
- 移除 StrBuiltinArgs/strArgsReady 的首次执行参数解析缓存；字符串内置命令使用装载期函数调用 AST。
- STRFORM 静态字面量在 ExpressionParser 中编译为 FORM 节点；变量参数返回原始文本。
  STRLENFORM/STRLENFORMU 求值器不再二次解析求值结果。
- HTML_PRINT 只使用装载期模板；字符串变量/函数表达式编译为取值片段，其返回标签作为原始文本。
  ConsoleBackend 仅消费已求值的 PrintTemplate，不提供运行时 HTML 字符串编译 API。
- ExecutionEngine、ScriptRunner、ExpressionEvaluator 不再调用 StrFormParser::parse 或 HTML 模板 compile。
  调试器/交互输入的表达式前端与显式文本处理（正则、SPLIT、HTML_TAGSPLIT 等）仍执行语言运算，
  不属于将变量结果重新解释为 ERB/FORM 源码。
- 严格 FORM 类型保留：数字插值用 `{A}`，字符串插值用 `%S%`；修正旧测试中 `NOPE_%A%` 为 `NOPE_{A}`。
- 验证：完整构建、42/42 ctest、git diff --check 通过；测试覆盖 CALLFORM/TRY/CALLF、
  STRFORM 字面量与变量、静态 HTML 与变量原始文本、字符串赋值和 PRINTFORMS 分片。

## 整数字面量 Signed64 / Bits64（2026-10-10）

- LiteralNode 使用 std::variant<qint64, quint64> 保存整数，分别表示 Signed64
  数值与 Bits64 位模式；语言层 OperandType 仍为 Int，字符串类型约束不变。
- NUMBER token 在装载期保存转换后的 64 位数据。指数归约使用精确整数乘除，
  允许正数指数结果覆盖全部 64 位位模式，因此 1p63 保存为 Bits64，
  integerBits() 为 0x8000000000000000。1p64 仍拒绝，未引入任意精度整数。
- intValue() 显式通过 std::bit_cast 将 Bits64 解释为二补码有符号值，
  供现有 ERB 运算器、变量存储、函数参数接口使用。未改变这些接口的有符号语义，
  也未把 Bits64 当成通用无符号算术类型；不使用 double 转换。
- AST JSON 输出 integerRepresentation 与十六进制 integerBits，保留表示信息。
- 回归覆盖 1p63 的 Bits64 标记、最高位位与结果、1p64 拒绝；42/42 测试通过。
- Megaten CLI 装载完成 8344 脚本、14514 变量、340 告警，日志中没有 1p63
  转换/归约错误。验证进程在进入游戏循环后由 90 秒 timeout 结束。

## eraTW 条件 FORM 与标题图片（2026-10-10）

- FORM_STR 命令优先按参数规范解析整段，避免以\@条件\@开头的
  REUSELASTLINE、DATAFORM 等被误送普通表达式前端，丢失条件后的文本与插值。
  真实 eraTW 装载 2258 脚本、9739 变量、0 告警，进入标题等待输入。
- HTML_PRINT 字符串加法在装载期展开为静态标签片段及动态表达式引用。
  eraTW 标题的固定 p/img 标签直接编译为图片模板，TOSTR(LOCAL, "000")
  只在运行期求值 src 属性。不重新解析运行期字符串；裸变量返回标签仍保持文本。
- 图片跨舞台边界时保留仍与视口相交的锚点行，QML 使用 displayMargin
  绘制视口外锚点的溢出部分；舞台快照更新后完成布局并锚底。
- EraRender 在 backend 延迟注入时也开启舞台模式。
- 回归覆盖条件 FORM 后缀、HTML 拼接动态图片名、图片跨舞台边界；42/42 通过。

## VARSIZE 命令的数组身份（2026-10-10）

- Megaten OPTION.ERB:258 为 VARSIZE ARG_COM，ARG_COM 是二维 REF 字符串数组。
  根据 test/data/commands/VARSIZE.md，此处传数组本身，省略下标合法。
- 命令改为 ArgKind::Var（一个变量参数），不再构造 VARSIZE(...) 函数调用；
  装载校验忽略该命令的元素索引，普通元素读取仍要求多维下标完整。
- 执行从已有 VariableNode 获取数组身份，解析 REF 目标和私有作用域，按声明
  或存储形状将各维长度写入 RESULT:0..2，不求值索引或索引内的副作用。
- 回归覆盖二维 REF、省略下标、忽略索引副作用、未写入数组的声明长度；42/42 通过。
- 真实 Megaten 装载8344脚本、14514变量、52告警，OPTION.ERB:258 告警消失。


## 运行期解析入口清理（2026-10-10）

依据 `test/data/commands/CASE.md`、`test/data/language/表达式.md`、
`test/data/functions/STRFORM.md`。reference 与 test/data 保持原样。

- ExpressionEvaluator 的 QString 适配器只查询已装载 AST；删除其内部 Lexer、
  ExpressionParser 和未使用的 parseAndEvaluate。未装配 provider 或缓存未命中返回无效值。
- QString 兼容适配器使用 loadedExpressionAst 只读查表，不在运行期解析或
  重绑定作用域；执行器的 BAR/颜色/字体/显示等命令直接消费 Operand.ast，
  已删除 evalExpressionCached helper，避免装载结束清空作用域缓存后执行失败。
- CASE 在 AstBuilder 装载时生成条件，在 finalizeParse 以 ownerFunction 重建；
  ScriptRunner::caseMatches 不再构建语法结构。
- 连续数值和字符串表达式赋值的 RHS 列表保存为 LogicalLine::assignmentAsts；
  执行期不再 assignmentValues 分割源码。字符串 +=、*= 直接组合节点，
  数值复合赋值通过固定 opcode 映射调用运算节点，不再调用 Lexer。
- SPLIT 写回元素数量直接写变量值，不再拼出数字表达式交给赋值解析。
- STRFORM 保持 StrFormNode 的 rawText/node 双行为：直接 FORM 求值展开装载节点，
  普通字符串消费者读取 rawText。运行期变量/函数产出字符串按数据使用，不二次解析。
  这与 C# STRFORM 可以对运行期字符串进行 FORM 解析的行为存在有意差异。
- 保留入口：装载期 expressionAst/InScope、ExpressionParser/Lexer、StrFormParser、
  ArgumentParser（声明/类型/源码绑定）；QString 求值仅兼容已装载 AST 查询；
  TOINT/ISNUMERIC、SPLIT、正则、HTML_TAGSPLIT 属于显式数据处理。
  左值及 SPLIT/HTML_TAGSPLIT 内部写回均消费变量节点或已经求值的 LhsRef，
  不再拆分变量引用源码。

回归覆盖缓存未命中、未装配 provider、已装载 AST 查询、CASE 装载缓存，
以及改写 raw 后的连续赋值；既有 FORM/STRFORM 双行为回归继续运行。


## 左值、音频、DataTable 与调试编译入口完成迁移（2026-10-10）

依据 `test/data/commands/{PLAYSOUND,PLAYBGM,SETBGMVOLUME,SETSOUNDVOLUME,DT_COLUMN_OPTIONS}.md`、
`test/data/commands/CASE.md` 与表达式文档；参考树和 test/data 未修改。

- 删除 ExecutionEngine::parseLhsRef、冒号拆分 helper、parseLHS、ScriptRunner 的
  extractVarRef 原文回退与 bareVarName。左值接口改为接收 ExpressionNode，必须是
  VariableNode；下标统一由 ExpressionEvaluator::resolveIndices 消费已有节点，含 CSV
  字符串映射、变量和表达式副作用。字符串赋值/复合赋值的左值下标仅求值一次。
- FOR LoopFrame 保存装载期 counter 操作数，NEXT/BREAK 读取并写回同一引用；
  FOR A:B 不再丢失下标，行移动后修改计数器的循环正确重检相邻行。
- PLAYBGM/PLAYSOUND/SET*VOLUME 增加参数 schema，音频扩展只消费参数 AST，
  删除 statementRemainder、splitTopLevelComma、audioEval、去引号/原文求值回退。
  PLAYSOUND 重复次数仍为可选整数，默认 1，参数中的副作用只求值一次。
- DT_COLUMN_OPTIONS 在 AstBuilder 装载期解析字符串表名/列名及有序 DEFAULT 节点；
  支持多个重复 DEFAULT（最后一次生效）、嵌套表达式及字符串内逗号/等号。
  未知选项、空值、非法名字类型在装载期诊断；运行期类型不匹配、缺失 AST、
  未知表/列报错，禁止把原文或失败值转成默认 0。
- 扩展函数语句不再调用 evaluate(raw)；CASE 缺少条件节点立即报错。
  ConsoleBackend::printHtml(QString) 编译便利入口已删除，后端只消费 PrintTemplate；
  布局测试显式在夹具阶段编译 HTML。DT_SELECT 的字符串 DSL、DT_FROMXML 的 XML、
  输出按钮标记与 SPLIT/HTML_TAGSPLIT 保持为显式数据操作。
- DebugDialog 继续调用 VariableStorage::evaluateExpression。该入口显式编译即时输入，
  包括字符串类型和 FORM 子表达式，然后调用 AST 求值；与脚本 QString 适配器的
  只读查询路径隔离。非法 debug 输入返回无效值，未装配脚本 provider 不会开始解析。
- 修正打印测试作用域夹具：表达式在哪个作用域装载，就在该作用域查询；连续 LOCAL
  赋值测试先分配第二槽，保留已有数组长度边界校验，不放宽越界行为。

验证：cmake --build build -j6 成功；ctest --test-dir build --output-on-failure 42/42 通过；
另外单独运行 test_extension_registry（未登记到现有 42 项 CTest）通过，覆盖动态表/列名、
重复 DEFAULT、原文篡改、非法选项、缺失 AST 和 debug 即时表达式/FORM。
音频测试覆盖原文篡改、变量文件名、整数表达式、重复次数副作用和缺失 AST。
打印测试覆盖左值原文篡改、字符串复合赋值下标副作用和缺失左值 AST。
不代表已验证所有真实游戏交互；未提交，未添加 PRD、参考游戏或构建输出。

### 最终块结构与默认字符串补漏

- PRINTDATA/STRDATA 全部变体的 DATALIST/ENDLIST/ENDDATA 分组和结束位置在
  EraParseTable::loadScript 构建；删除首次执行扫描和 mutable 解析缓存。
  回归验证装载后即可读取完整组索引，重复装载不会重复追加。
- RETURNF、CALL 实参、输入缺省值、POWER、ASSERT 和存档索引删除 raw 求值回退；
  缺失表达式 AST 明确失败。字符串字面量和已求值的传输值仍作为数据读取。
- 函数默认字符串的外层引号在装载形参声明时处理，语句调用与式中调用只读保存值。

最终验证：cmake --build build-debug -j4 成功；offscreen ctest 42/42 通过；
独立 test_extension_registry ALL PASS；git diff --check 通过。

## HTML 装载期完整结构与字符串解析边界（2026-10-11）

- HTML 模板编译器仅接受 AST，删除公开 `compile(QString)` 入口；脚本执行器、表达式求值器和控制台没有运行时模板编译入口。
- 字符串字面量、字符串加法和 FORM 的静态文本统一在装载期展开，允许标签和属性跨这些 AST 边界组合；插值保持独立节点，求值后的标签和实体不再解释。
- 注释剥离尊重转义引号及完整 FORM 跨度，HTML 实体分号和 FORM 内嵌引号不会截断后续标签；宏定义采用相同规则。
- 不完整标签保留全部尾部文本，遇到下一个未被引号包围的 `<` 后恢复扫描，不吞掉后续完整标签；动态标签名保持文本。
- 运行时仍支持明确的数据操作（SPLIT、正则、XML、HTML_TAGSPLIT、TOINT 等）和调试器显式输入编译；这些不会把脚本变量字符串重新编译为 ERB、FORM 或显示 HTML。
- 验证：完整构建、44/44 CTest、git diff --check 通过。包含混合 FORM 拼接、跨节点标签、动态属性、格式宽度、副作用只求值一次、不完整标签和原始动态文本回归。
