# 参数解析与 FORM 修复（2026-10-08）

C# 语义依据：`test/data/commands/{CUSTOMDRAWLINE,CASE,TIMES,ENCODETOUNI}.md`、
`test/data/language/表达式.md` 及对应参考源码。以下记录 C++ 移植改动。

- `CUSTOMDRAWLINE` 按单个原始文本参数处理，保留逗号、引号和尾部空白，空参数产生诊断。
- `CASE` 按值、闭区间 `TO`、`IS` 比较条件解析；装载期与运行期共用条件拆分，运行期在完成声明处理后重建缓存。
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
