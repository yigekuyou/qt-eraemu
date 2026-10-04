# test/ —— era 全函数覆盖示例与回归

本目录是 **emuera（Qt/C++ 移植）** 的端到端回归测试：一个可运行的 era 游戏
（`test/example/`），逐项覆盖 Emuera 的命令与式中函数，并对关键语义做断言。

设计原则：**用例清单来自 C# 权威源码**（Emuera 原版 + EmueraEE），
不依赖本移植的 C++ 表 —— 这样才能反过来发现移植缺了什么。

## 目录

| 路径 | 作用 |
| --- | --- |
| `example/` | 可运行的 era 游戏（CSV + ERB），测试载体 |
| `example/ERB/0N_*.ERB` | 手写测试组（语义断言） |
| `example/ERB/10_COVERAGE.ERB` | **自动生成**的全函数冒烟覆盖（勿手改） |
| `data/emuera_standard_cmds.txt` | Emuera 原版命令清单（导出自 C#） |
| `data/emuera_standard_funcs.txt` | Emuera 原版式中函数清单（导出自 C#） |
| `data/emuera_ee_cmds.txt` | EmueraEE 扩展命令清单（**命令/桩与式中函数都在扩展侧** `src/eraengine/GameProc/ee_extension.cpp` 经注册类 `ExtensionRegistry`（`reg`/`regForm`/`regExpr`）逐一登记；式中函数如 `EXISTFUNCTION`/`GETDOINGFUNCTION`/`GETDISPLAYLINE` 亦在此实现，声明由注册类注入运行期扩展函数表。**扩展系统变量**（`DAYNAME`/`TIMENAME`/`MONEYNAME` 及 `DAY`/`TIME`/`MONEY` 名表）亦在此经 `regVariable`/`regNameTable` 登记进运行期扩展变量表 `system_variables.h`。本文件仍为覆盖组名单来源） |
| `data/coverage_report.txt` | 覆盖率报告（生成） |
| `run_example.sh` | 运行示例（唯一需要的入口） |
| `export_command_tables.py` | 从 C# 源码导出上述命令清单 |
| `gen_coverage.py` | 依据清单生成覆盖组 + 覆盖率报告 |

## 快速开始

```bash
# 1) 编译（含测试用 CLI）
cmake --build build --target test_cli

# 2) 运行全部自动组（1..10、14、16、17、23..28 + 汇总）
./test/run_example.sh

# 3) 只跑某一组
./test/run_example.sh 5     # TRY*LIST + FUNC/ENDFUNC
./test/run_example.sh 11    # 输入族（需 test_cli 自动喂输入）
```

退出码：`0` = 渲染自检无异常 **且** 全部断言通过；非 0 = 有失败。

## 测试组

| 组 | 内容 |
| --- | --- |
| 1 | 变量 · 赋值 · 数组 · 常量 |
| 2 | PRINT 全族 · 按钮 · 颜色 · 对齐 · REDRAW · HTML 系 |
| 3 | 控制流 IF/SIF/SELECTCASE/循环/GOTO |
| 4 | 调用 CALL/CALLFORM/TRYCALL/JUMP 系/CALLF/RETURN |
| 5 | **TRYCALLLIST / TRYJUMPLIST / TRYGOTOLIST / GOTOLIST + FUNC/ENDFUNC** |
| 6 | 字符串函数 |
| 7 | 数值函数 · 随机数控制 |
| 8 | CSV / 角色函数 · 角色列表操作 |
| 9 | 存档 SAVE/LOAD/DEL/CHK · SAVEGLOBAL · SAVETEXT/LOADTEXT · SAVECHARA/LOADCHARA |
| 10 | **全函数覆盖**（依据 C# 命令表自动生成的冒烟段） |
| 11 | 输入族 · 等待（`INPUT`/`INPUTS`/`ONEINPUT`/`TINPUT`/`WAITANYKEY`/`AWAIT`…） |
| 12 | `BEGIN`（破坏性：切换流程、不返回） |
| 13 | `THROW`（破坏性：主动报错终止，**预期**「执行出错」） |
| 14 | 剩余命令（`TRYGOTO`/`JUMPFORM`/`TRYCJUMPFORM`/`PRINT_RECT`/`ADDDEFCHARA`/`CALLEVENT`…） |
| 27 | **文档语义·表达式/字面量/声明**（`example/ERB/TEST_HEADER.ERH` + `27_DOC_EXPR.ERB`，ecd/docs 规范 + C# 语义） |
| 28 | **文档语义·SELECTCASE/循环/EE 与 eraTW 惯用法**（`28_DOC_FLOW.ERB`） |
| 29 | **文档语义·BEGIN FIRST 事件函数流** `#PRI/#LATER/#SINGLE/#ONLY`（破坏性·单独跑，`29_DOC_EVENT.ERB`） |

「全部自动运行」（`./test/run_example.sh` 无参数）依次执行：
**1–10、14、16、17、23–28 + 汇总**。

不在自动路径、需单跑的组（`./test/run_example.sh <组号>`）：

| 组 | 单跑原因 |
| --- | --- |
| 11 | 输入族，需 `test_cli` 自动喂输入 |
| 12 | `BEGIN`：切换流程、不返回 |
| 13 | `THROW`：**预期**「执行出错」 |
| 15 | 鼠标/原始输入：`k` 注入 + 超时两条路径 |
| 18 | `RESTART` + EE 破坏系 |
| 19 | `DOTRAIN`：**预期**「执行出错」 |
| 20/21 | `RESTART` 菜单复刻 / GOTO 标签作用域 |
| 22 | MAP 绘制复现（计时用） |
| 29 | `BEGIN FIRST` 事件函数流（破坏性） |

注意：单跑组若未调用 `TEST_SUMMARY`（组 1–10 等子集）或本就**预期出错**
（组 13/19），`run_example.sh` 的汇总行会显示「失败」——这是外壳判定
（它靠输出里的 `全部断言通过` 判定），看 `[FAIL]` 与否才是真正的断言结果。

## 文档语义测试（组 27–29）

组 27–29 的用例**全部推导自 ecd/docs 规范文档与 C# 权威源码 / eraTW 自带
EmueraEE·私家改造版 readme**，不参考本移植实现：

* 组 27：`ecd/docs/reference`（表达式/语句/结构/变量/版本索引）、`spec/EraBasic`、
  Emuera C#（`OperatorCode`/`CreateBar`/`EraStreamReader`/`ErbLoader` 等）。
  覆盖：进制字面量、`^^`/`!&`/`!|`、字符串重复 `\@…\@` 三元、FORM 对齐、
  `__INT_MAX__` 系常量、`;!;` 行、行连接 `{}`、ERH 宏与广域/全局变量、
  `#LOCALSIZE`、LOCAL 静态保留、参数初始值、REF 引用传参、`'=`、
  BARSTR/GETTIME/GETEXPLV/POWER/RESET_STAIN/SAVEDATA_TEXT、
  STRJOIN/ARRAYREMOVE（v18 语义）、UNICODE 控制码（v18 语义）。
* 组 28：eraTW ERB 惯用法（CASE 多值+区间、SIF…RETURNF 链、TRYCCALLFORM、
  CSV 名下标、`#DIMS` 初始化列表、全角名变量）+ EE readme（EXISTFUNCTION、
  TRYCALLFORMF、EXISTSOUND、GETDISPLAYLINE、GETDOINGFUNCTION、BINPUT 缺省值）。
  新增 `example/CSV/DAY.csv` 验证 DAYNAME。
* 组 29：`ecd/docs「ERB 的内置流程」`——BEGIN FIRST 触发 @EVENTFIRST、
  PRI→普通→LATER 顺序、#SINGLE 返回 1 跳过本组、#ONLY 终止事件。

这些断言是「规范行为」的编码。**截至 2026-10 已全部通过**：组 27/28 纳入
「全部自动运行」，组 29 单跑（`./test/run_example.sh 29`），二者均
`★ 全部断言通过（ALL PASS）★`、`./test/run_example.sh` 退出码 0。

本轮（2026-10）为让 27–29 转绿而对移植做的修复：

* **ERH 对象宏**：`#DEFINE NAME body` 的替换体未剥离宏名，`body` 错成
  `"NAME body"` 并自引用膨胀（`DOC_HELLO "你好宏" "你好宏"…`、`DOC_MAC_27`→0）。
* **SAVEDATA 字符串实参**：`SAVEDATA 40, "标题"` 的第二实参是整段引号字面量
  （已去引号、无 AST），此前被当表达式求值成 0 → `SAVEDATA_TEXT` 落空。
* **`DAY.csv` 名表（已移入扩展）**：`DAYNAME`（含 fork 一并实现的 `TIMENAME`/
  `MONEYNAME`）与 `DAY/TIME/MONEY → *.CSV` 名表映射由 **EmueraEE 扩展**登记
  （`ee_extension.cpp` 经 `ExtensionRegistry::regVariable`/`regNameTable` →
  运行期扩展变量表），不再写死进核心：`system_variables.h`（类型表）、
  `variable_config.cpp`（默认尺寸）、`constant_table.cpp`（名表映射）现只保留
  **原生**条目。另修 `RESETGLOBAL` 不再清空 `<VAR>NAME` 名表（组 9 的
  `RESETGLOBAL` 曾把 `DAYNAME` 清掉）。
* **EE 式中函数**：`EXISTFUNCTION`（1/2/3/0 + 第二参大小写选项）、
  `GETDOINGFUNCTION`、`GETDISPLAYLINE`（按逻辑行、0 起算、越界空串）——
  **实现全在扩展侧**（`ee_extension.cpp` 经 `ExtensionRegistry::regExpr` 登记，
  声明注入运行期扩展函数表；原生函数表不变），引擎只提供 services 与
  `BuiltinOp::Extension` 求值回调；同时支持伪变量 0 参写法 `LINECOUNT()` 与
  字符串赋值右值的**单个函数调用**（`RESULTS:0 = GETDOINGFUNCTION()`）。
* **`BINPUT`/`BINPUTS`**：无按钮时直接把缺省值写入 `RESULT(S)`（不等待）；
  裸执行（无按钮、无缺省值）按输入族桩容错跳过、不报错（组 15）。
* **`BEGIN`**：此前会 `returnFromCall()` 回到调用方（菜单继续跑到 `ONEINPUT`），
  导致 `BEGIN FIRST` 不触发 `@EVENTFIRST`；改为对齐 C#：清空调用栈、`Halt`
  交还系统层。事件分组同步改为 `#PRI → 普通 → #LATER`，`#ONLY` 为「该函数
  返回后终止整个事件」的标记（组 29）。

## eraTW「採集場所一覧」回归（2026-10）

用 `test_cli` 复现 eraTW `1,0,410`（`--script 1,0,410,99`）时发现「选项是数字、
输入 99 无法返回」。定位到**两个独立缺陷**，均按 C# 权威语义修复：

* **标签名大小写不敏感**（`era_parse_table.cpp`）：`labelPositions` 原以标签
  原样大小写为键，而式中调用用户函数的求值回调会把名字 `toUpper()` 后再查
  （`ExpressionEvaluator` → `ScriptRunner::invokeUserFunction` → `callLabelWithReturn(info->name)`），
  于是**混大小写**的 `@ForagePlaceName` 恒查不到 → `%ForagePlaceName(x)%`
  恒返回 0：`@SHOW_GATHERING_LIST` 里 `IF ForagePlaceName(PLACE) != ""` 对
  99 也成立 → 不进 `RETURN -1` 分支 → 无法返回。改为存储/查找统一折叠大写
  （对齐 C# `ErbLoader.LabelDic` 以 `ToUpper` 为键）。
* **字符串 `=` 赋值右值 = 格式化串**（`execution_engine.cpp::handleStringAssignment`）：
  裸文本一律字面量（对齐 C# `AnalyseFormattedString`），只有裸**字符串**变量名
  才按变量引用求值。旧实现把「任意已知变量/系统变量/角色变量」都当引用，
  于是 eraTW `DIM.ERH:91 #DIM CONST 斜角的竹林 = 430`（地图地点编号）与
  `@ForagePlaceName` 的 `LOCALS = 斜角的竹林`（地点名字符串）**同名碰撞**时，
  取到整数常量 430 → 「採集場所一覧」整列显示 430/460/470… 数字。

回归：`test_user_functions`（§7 混大小写式子调用）、`test_statements`（§9
整数名当字面量、字符串名当引用）。

## 关于「自动输入」

`INPUT` / `INPUTS` / `ONEINPUT` / `TINPUT` / `WAITANYKEY` / `AWAIT` 在 GUI 下会
挂起等玩家点击，**只有 `test_cli` 能用 `--script` 序列自动喂入**。这正是
`run_example.sh` 只能配 `test_cli` 跑完整流程的原因：

```bash
./build/src/eraengine/test_cli test/example --script 0,0,0,0,0,0 --frames 400 --check
```

## 覆盖率（依据 C# 表）

```bash
python3 test/export_command_tables.py   # C# 源码 -> data/*.txt
python3 test/gen_coverage.py            # 生成 10_COVERAGE.ERB + 报告
python3 test/gen_coverage.py --check    # 只出报告
```

命令清单来自：

* **Emuera 原版**：`Emuera/GameProc/Function/BuiltInFunctionCode.cs`（`enum FunctionCode`）
  与 `Emuera/GameData/Function/Creator.cs`（`methodList`）；
* **EmueraEE 扩展**：EE 发行版自带文档（`eraTW/README集/EmueraEE Readme/`）里的
  新增命令（EE 未附带 C# 源码，清单见 `export_command_tables.py` 的 `EE_EXTENSIONS`）。

当前覆盖（详见 `data/coverage_report.txt`）：原版命令 199 条、式中函数 163 条、
EE 扩展 56 条，**全部 0 未覆盖**。少量命令刻意不由自动段执行（声音/文本框/多列/
内存等需要运行环境，或会改流程），报告中逐条给出原因。

## 相关

* 编译器/引擎回归：`cd build && ctest`（**28/28 通过**）。
* 本目录的改动同时记录了若干引擎修复：`CHKDATA` 返回值（EraDataState）、
  `RESETDATA` 清空角色、`RESETGLOBAL` 保留函数私有变量、`QUIT` 结束程序、
  `SAVETEXT/LOADTEXT` 的 `txt{nn}.txt` 语义、`CHKFONT` 无 GUI 时的崩溃等。
