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
| `example/ERB/35_DOC_SMOKE.ERB` | **自动生成**的文档语义冒烟组（由 `data/doc_smoke.tsv` 渲染，勿手改） |
| `data/emuera_standard_cmds.txt` | Emuera 原版命令清单（导出自 C#） |
| `data/emuera_standard_funcs.txt` | Emuera 原版式中函数清单（导出自 C#） |
| `data/emuera_ee_cmds.txt` | EmueraEE 扩展命令清单（**命令/桩与式中函数都在扩展侧** `src/eraengine/GameProc/ee_extension.cpp` 经注册类 `ExtensionRegistry`（`reg`/`regForm`/`regExpr`）逐一登记；式中函数如 `EXISTFUNCTION`/`GETDOINGFUNCTION`/`GETDISPLAYLINE` 亦在此实现，声明由注册类注入运行期扩展函数表。**扩展系统变量**（`DAYNAME`/`TIMENAME`/`MONEYNAME` 及 `DAY`/`TIME`/`MONEY` 名表）亦在此经 `regVariable`/`regNameTable` 登记进运行期扩展变量表 `system_variables.h`。本文件仍为覆盖组名单来源） |
| `data/coverage_report.txt` | 覆盖率报告（生成） |
| `data/doc_smoke.tsv` | 文档语义冒烟的调用清单（经语义复核，`mode=skip` 的条目注明原因；第 6 列为注入输入） |
| `run_example.sh` | 运行示例（唯一需要的入口） |
| `export_command_tables.py` | 从 C# 源码导出上述命令清单 |
| `gen_coverage.py` | 依据清单生成覆盖组 + 覆盖率报告 |
| `gen_doc_smoke.py` | 从 `data/**/*.md` 的签名与用法合成调用 → 草稿 + 渲染 `35_DOC_SMOKE.ERB` |
| `merge_doc_smoke.py` | 合并语义复核查出的修正行，回写 `data/doc_smoke.tsv` |
| `check_doc_smoke.py` | 组 35 的静态校验（块配平/未知语句头/未声明变量/清单一致，**不执行 ERB**） |

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
| 30 | **音频·图片（用命令随机生成素材）**（`30_ASSET_GEN.ERB`） |
| 31 | **GETCONFIG/GETCONFIGS（emuera.config 取值）**（`31_GETCONFIG.ERB`） |
| 32 | **通用图像处理（G / SPRITE / CBG 全族 + 真实图像文件 webp）**（`32_IMAGE.ERB`，素材 `example/resources/`；由原「组 32 CSV 精灵偏移」与「组 34 真实图像文件（webp）」合并，作者声明见 `example/resources/README_webp.md`） |
| 33 | **`END` 是变量（`#DIM END`）不是指令**（`33_END_VARIABLE.ERB`，eraTW 角色移動 死循环回归） |
| 35 | **文档语义冒烟**（`35_DOC_SMOKE.ERB`：由 `data/` 的 451 篇语义文档逐条生成调用——签名与参数取自文档，会写盘/结束程序/依赖音频 GUI 的条目留 `; SKIP` 注释并注明原因；需交互的条目在清单第 6 列给出要注入的输入。**单跑**：`./test/run_example.sh 35`） |

「全部自动运行」（`./test/run_example.sh` 无参数）依次执行：
**1–10、14、16、17、23–28、30–33 + 汇总**。

不在自动路径、需单跑的组（`./test/run_example.sh <组号>`）：

| 组 | 单跑原因 |
| --- | --- |
| 11 | 输入族，需 `test_cli` 自动喂输入 |
| 35 | 文档语义冒烟：需按 `data/doc_smoke.tsv` 第 6 列注入数值/字符串/鼠标（`run_example.sh 35` 已内置序列） |
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

## eraTW「画像表示設定」示例立絵回归（2026-10）

用 `test_cli` 复现 eraTW `1,0,400,97`（`--script 1,0,400,97,0` 打开画像显示）
时发现「示例立絵的图片没有」（`▼示例▼` 之后一行空白，无 `part image`）。

根因：**`#DIM REF` 形参绑定到系统变量时读不到值**。`@PRINT_TARGET_IMAGE(ターゲット)`
用 `#DIM REF ターゲット` 接收系统变量 `TARGET`（`CALL PRINT_TARGET_IMAGE(TARGET)`）。
REF 别名机制（`VariableStorage::setReference`）只对**用户全局槽**生效，而系统变量
按**名字**分派到各自数组（`m_target` / `m_flag` …）。于是函数体内 `SIF !ターゲット`
经 `getGlobalInt1D("ターゲット")` 落到同名用户全局槽（恒 0）→ 提前 `RETURN` →
示例立絵整块不显示。

修复（`variable_storage.{h,cpp}` + `expression_evaluator.cpp::readIntArray`）：
新增 `VariableStorage::systemVariableName(name)`，把名字先经 REF 引用表解析，
`hasSystemVariable` / `getSystemVariable` / `setSystemVariable` 与整数组读取
（长度 + 元素）都改用解析后的名字 —— REF 到系统变量的读写回到系统变量通道。
`test_cli eraTW --script 1,0,400,97,0 --model` 现可见
`part image src=立絵_裸_笑顔_1`。

回归：`test_user_functions`（§8 REF 形参 ← 系统变量 TARGET）。

## eraTW「画像表示設定」的选项 4/5/6（2026-10 修正）

打开 `1,0,400,97`（`--script 1,0,400,97,4,5,6`）后，画面上的「画像尺寸」档位
（选项 4：`[180]→[270]→…`）与「拡大/縮小」（选项 5/6：`100％→110％→…`）
**状态确实在变、显示文字也在变，但 `▼示例▼` 立絵一点都不跟着缩放** ——
看起来就是「4 5 6 无效」。

抓法：`QT_LOGGING_RULES="era.trace.debug=true"` 看 `DEBUGPRINTFORML` 打出的
实际 HTML，得到

```
<img src='立絵_裸_笑顔_1' srcb='立絵_裸_通常_1' height='0' width='0' ypos='0'>
```

即 `{画像縦幅}` / `{画像横幅}` 全是 **0**（于是渲染回落到精灵原始尺寸）。

根因：**`GETCONFIG` / `GETCONFIGS` 完全没接线**（`setConfigProvider` 只在
单测里被调用过，引擎装配里没有）。eraTW 的像素换算全建立在它上面：

```erb
角色画像表示尺寸比 = 100 + isModifier
SIF MIN(FLAG:画像表示,ターゲット人数) * (默认角色画像横幅 * 角色画像表示尺寸比/100) > GETCONFIG("ウィンドウ幅")
	角色画像表示尺寸比 = 100 * GETCONFIG("ウィンドウ幅") / …
画像縦幅 = (默认角色画像横幅 * 拡大比率) / GETCONFIG("フォントサイズ") * …
```

`GETCONFIG(…)` 一律 0 → `角色画像表示尺寸比` 被上面的「窗口宽限幅」算成 0
（`… > 0` 恒真）→ 除以 `フォントサイズ`=0 又走引擎的「除零得 0」保护 →
`height='0' width='0'`。于是选项 4/5/6 改的 `默认角色画像横幅` / `isModifier`
根本进不到 `<img>`。

修复（`Config/config_loader.{h,cpp}` + `eraengine.cpp`）：
新增 `ConfigLoader::configValueInErb(key, out)`，对齐 C# `ConfigData.GetConfigValueInERB`
的**白名单**（只有列出的配置项可被 ERB 读出），并按项类型决定返回值形态 ——
`<bool>` → `"1"/"0"`、`<Color>` → `((R*256)+G)*256+B`、`<int>/<Int64>` → 数值、
`<string>/<char>/<TextDrawingMode>` → 原样文本；缺项回落 C# 内建默认
（フォントサイズ 18 / 一行の高さ 19 / ウィンドウ幅 760 / 描画インターフェース
TEXTRENDERER …）。引擎装配里把它接到 `ExpressionEvaluator::setConfigProvider`。
eraTW 的 `emuera.config`（フォントサイズ 16 / 一行の高さ 16 / ウィンドウ幅 1400 /
描画インターフェース TEXTRENDERER）现在能真正读到。

验证（`--model`，`▼示例▼` 的 `part image src=立絵_裸_笑顔_1`）：

| 操作 | 显示 | 图片 cols |
| --- | --- | --- |
| 初始 | `[180]：100％` | 23 |
| 选项 4 | `[270]：100％` | 34 |
| 选项 5 | `[270]：110％` | 37 |
| 选项 5 | `[270]：120％` | 41 |
| 选项 6 | `[270]：110％` | 37 |
| 选项 6 | `[270]：100％` | 34 |

（`height/width` 属性单位是「1/100 字号」，故 `180*100/16 = 1125` 恰好等于
180px 立絵在字号 16 下的原始宽度 —— 100％ 时与修复前外观一致，只有 4/5/6
才看得出变化。）

**同一页其它选项**（逐个用 `--script` 喂入 + `:e` 读状态核对，均正常）：

| 选项 | 效果 | 实测 |
| --- | --- | --- |
| 0 | `FLAG:画像表示` 开关 | 画像显示 ↔ 画像不显示 |
| 1 / 2 | 显示人数 ±（clamp 1..10） | 1→2→3 / 3→2→1（到 1 停） |
| 3 | `FLAG:立絵種類` | `[立絵]` ↔ `[顔]` |
| 4 | 尺寸档位 180→270→360→540→720（多尺寸开关） | `[180]`→`[270]`（默认角色画像横幅） |
| 5 / 6 | `isModifier` ±10（clamp ±50） | 100％→110％→120％ / 回落 |
| 7 | `FLAG:画像表示位置` | `FLAG:画像表示位置` 0→1 |
| 8 | `FLAG:画像枠表示` | 图像边框 OFF→ON |
| 9 | `FLAG:巨乳差分表示` | 0→1 |
| 10 | `FLAG:追加エフェクト` | 不显示→显示 |
| 11 | `FLAG:自慰画像非表示` | 不显示→显示 |
| 12 | `INVERTBIT 顔絵付きメッセージ,0` | 无→有→无 |
| 13 | `FLAG:挿絵表示` | OFF→ON |
| 14 | `FLAG:射精画像種類` ++（≥3→0，仅挿絵 ON 时） | 断面＋効果音→只有断面→只有効果音→回环 |
| 15 | `FLAG:アニメーション` ++（≥6→0） | 重複 1回→2回→3回 |
| 16 | `INVERTBIT FLAG:挿入アニメ过滤器,0` | `[〇]`↔`[×]` |
| 999 | `VARSET TARGET` + `RETURN 0` | 返回上一级 |

回归：`test_getconfig`（白名单 / 类型 / 默认值 / eraTW 除零回归）。

> 备注：`--check` 对 eraTW 的**开头几帧**会报「屏幕空行过多（36/45）」，
> 这是标题/载入画面的启发式误报（与本次改动无关，已用 `git stash` 对照确认）。

## 跨行图片的「可见窗口」与 `<img ypos>`（2026-10 修正）

选项 4/5/6 改成按资源尺寸排版之后，立絵不再是一行高的方块，于是暴露出两个
**既有**缺陷（都发生在 `ConsoleBackend::visibleBlocks()`）：

### ① 「不完整显示立绘，立绘就消失」

可见窗口只遍历 `[windowFirstLine, 行数)` 这些**行**。跨行图片的锚点行一旦滚出窗口
顶部，整张图就不再产出区块 —— 即使它还有大半张露在可见区里。

修：窗口上方再多扫 `m_maxSpanReach` 行（缓冲里最长区块的「向上探出量」，含 ypos
负偏移），再按**绘制矩形与窗口相交**过滤（`topRow + rows > 0 && topRow < 窗口行数`）。
`row` 允许为负，交给 QML 视口的 `clip` 裁掉多余部分。

### ② 「边框没有在立绘边缘」

eraTW 的画像枠・時間停止・特效都是**另一张图叠在立絵上**：它们各自被打印在自己的
行里，靠 `<img ypos=N>`（N = 字号百分比，负值向上）被拉回去盖住立絵。

修：`ConsoleSpan::yposRaw` → `ConsoleLayout::measurePart` 折算成像素
（`top = raw_ypos * FontSize / 100`，对齐 C# `ConsoleImagePart`）→
`visibleBlocks()` 输出 `offsetRows`（= `top / 行高`）→ QML
`y = (row + offsetRows) * cellHeight`。

### 回归（都不需要跑 eraTW，毫秒级）

| 位置 | 断言 |
| --- | --- |
| `test_console_backend`「跨行图片：窗口裁剪 / ypos 图层叠加」 | `rows == 4` / `height == rows × 行高`；锚点行滚出窗口顶但仍有可见部分 → **必须产出区块**（`row == -1`）；整块在窗口外 → 不产出；`ypos=-800 → offsetRows == -8`；**框与立絵同一纵坐标**（叠加对齐） |
| `test_qml_console::test_imageBlockSpansRowsAndYpos` | QML 侧 `height == rows × cellHeight`（跨行）、`y == (row + offsetRows) × cellHeight` |

用 `:geometry` 复核（`dy` = `offsetRows`）：

```
[image] grid=(76,7) rel=(0,0) size=23x11 (px 184x176) dy=0    "立絵_裸_笑顔_1"
[image] grid=(76,8) rel=(0,0) size=23x11 (px 184x176) dy=-1   "フレーム_ターゲット_通常"
```

框锚点行 8 + dy(-1) = 7 = 立絵的 7 → 正好重合。`test_cli` 也补了
`:scroll <N>`（正 = 向上翻）用来复核历史视图的区块。

## CSV 精灵偏移与「尺寸头回退」（立绘合成 · 2026-10 修正）

eraTW 的立绘**不是一张图**：`ERB\リソース作成.ERB`（白蓮 55）与
`ERB\ステータス表示関連\モブ子表示.ERB`（路人）都是
「把 `/resources/` 图集里的部件叠进一张 G」再 `SPRITECREATE`：

```
CALL 画像合成(GID, "55_A1")   ->  GDRAWSPRITE GID, "55_A1", 0, 0, SPRITEWIDTH(...), SPRITEHEIGHT(...)
```

部件的落点全部来自 **CSV 第 7/8 列**（C# `AppContents.CreateFromCsv` 的
`tokens[6],tokens[7]` → `SpriteF.DestBasePosition`），例如
`差し替え画像/差し替え.csv` 的 `55_A1,55_別立ち.png,0,0,122,152,24,28`。

| 缺陷 | 现象 | 修正 |
| --- | --- | --- |
| 第 7/8 列被丢弃 | 所有部件都落在 (0,0)：**立绘不完整**、**差分图像盖不到该盖的地方** | 解析为 `Sprite.offsetX/Y` → `GraphicsStore::spriteBasePos` → `GDRAWSPRITE` 按 C# `ASpriteSingle.GraphicsDraw(g,destRect)` 缩放偏移（`+ off*目标尺寸/源尺寸`） |
| `ダミー.webp` 解不出来 | `resources/ダミー.webp` 是 34 字节、VP8L、180×180 的**全透明**图；Qt 的 webp 解码器读不了（C# 走 libwebp 直连能读）→ `SPRITEWIDTH("ダミー")=0` → 合成第一步 `GCREATE(GID,0,0)` 失败 → **整张立绘都建不出来** | `ResourceImageProvider::loadImageFile` 改为三级：Qt 解码 → **libwebp 直连**（对齐 C# `WebPWrapper`）→ 仍失败才按文件头（WebP VP8/VP8L/VP8X、PNG、JPEG、BMP、GIF）补齐尺寸、返回同尺寸全透明图兜底排版 |
| Qt 解不了但**有内容**的 webp 被静默替换成全透明 | 4×4 纯色等小图 Qt 读不了；若直接「按文件头回退全透明」，精灵图拼接就**缺块**，差分/特效画不出来（= 「图像覆盖」失败的根因） | 同上：先直连 libwebp 把真像素解出来，解不出才回退全透明 |
| `SPRITEPOSX/Y` 对静态资源返回 -1 | C# 返回 `DestBasePosition`（CSV 偏移），精灵不存在时返回 **0** | 同上；`SPRITEMOVE`/`SPRITESETPOS` 对静态资源也生效（C# 里静态精灵同样是可变对象） |

### CBG 家族（ClientBackground）对齐 C#

`Command.html`「图像处理相关」里的 `CBG*` 在 C# 侧是 `Creator.Method.cs` 的
`CBGSet*Method` / `CBGRemoveRangeMethod`（绘制在 `EmueraConsole.CBG_*`）。本轮把
返回值/条件对齐：

| 命令 | C# 语义（已对齐） |
| --- | --- |
| `CBGSETSPRITE` | 用 `AppContents.GetSprite`（**含 `resources/` CSV 静态资源**），不存在返回 0 |
| `CBGSETBUTTONSPRITE` | **只校验按钮值 `0..0xFFFFFF`**；精灵名允许空串/不存在（绘制时跳过空图）；第 7 参 tooltip 可省（`OmitStart=6`，6/7 参均可）—— 此前 C++ 表写成 7..7 参、且要求精灵已存在，均与 C# 不符 |
| `CBGSETG` / `CBGSETBMAPG` | G 未创建返回 0 |
| `CBGREMOVERANGE` / `CBGCLEAR` / `CBGCLEARBUTTON` / `CBGREMOVEBMAP` | **恒返回 1**（纯动作）；`CBGREMOVERANGE` 跳过 `zdepth==0` 的占位层 |

> 注意：CBG 的**渲染**（把层按 zdepth 与文字合并画在客户端区域）目前尚未被核心
> 渲染路径消费 —— `GraphicsStore::cbgLayers()` 只有数据层验证，QML 里还没有画它。

验证方式（**不跑 eraTW**，全部毫秒级）：

| 位置 | 断言 |
| --- | --- |
| `test_resource_image` §3/§4 | 第 7/8 列解析成 (3,2)／6 列旧写法 = (0,0)／非图集条目 = false；34 字节 webp `loadImageFile` 得 180×180 全透明、`intrinsicSize` 同值；**36 字节 4×4 纯品红 webp 解出真像素（= 图像内容没被回退丢掉）**；正常图不受影响 |
| `test/example/ERB/32_IMAGE.ERB`（组 32「通用图像处理」） | `SPRITEPOSX/Y` = CSV 列；`GDRAWSPRITE` 的落点 = 偏移（2 参与 6 参形态，放大时同比例）；**差分压在底图上的位置正确**；`SPRITEWIDTH("off_dummy")=180` 并能当合成画布；`SPRITESETPOS/SPRITEMOVE` 对静态资源生效；真实 webp（lossless 比色 / 有损只验尺寸）；颜色矩阵特效；`GDRAWG`/`GDRAWGWITHMASK`/`GSAVE`-`GLOAD`；精灵序列；`CBG*` 返回值 |
| 反证 | 关掉偏移应用 → 组 32 多条 FAIL（`偏移原点没有东西`、`差分没覆盖到的 (0,0) 仍是底图红` …）；关掉尺寸头回退 → `解码失败也得从文件头拿到宽 180` FAIL；关掉 libwebp 直连 → `纯色4x4 VP8L 真解码` FAIL |

## 音频播放（扩展能力 · 2026-10）

C# 原版没有音频；整块能力住在扩展（`GameProc/ee_extension.cpp`），经
`ExtensionRegistry` 登记，核心引擎不含任何音频后端。**语义以 EE 源码为准**
（`emuera.em` = 已克隆的 EmueraEE，`Runtime/Script/Statements/Instraction.Child.cs`）：

```csharp
public static Sound[] sound = new Sound[10];   // 10 条音效(SE)管线
public static Sound bgm = new();               // 1 条独立 BGM 管线
```

* **数量不硬编码**：`regAudioPipelines(quint32)` 由扩展声明 **SE 管线数**
  （对齐 EE `Sound[10]` = 10，**4 字节无符号**；没有程序同时播 2^32-1 条 BGM）。
  核心只搬运这个 `quint32`，不含「10」这个常量。管线布局：**0 = BGM**，
  **1..N = SE**。
* **C++ 控制 + QML 维护播放**：`GameView/audio_pipeline_pool.*`（`AudioPipelinePool`，
  经 `EraEngine::audio` 暴露给 QML）只**决定**第几条管线播什么、音量、循环次数，
  并以信号广播；`src/qml/AudioPlayers.qml` 按 `audio.soundPipelines` 维护对应数量的
  SE 播放器（`SoundEffect`）+ 1 条 BGM 播放器（`MediaPlayer`），播完回调
  `reportFinished` 归还管线。
* 命令（对齐 EE）：`PLAYBGM`（`STR_EXPRESSION`，无限循环）/ `PLAYSOUND`
  （`SP_HTML_PRINT`：字符串式 + 可选重复次数）/ `STOPBGM` / `STOPSOUND` /
  `SETBGMVOLUME` / `SETSOUNDVOLUME`（`INT_EXPRESSION`）—— 语句；
  `EXISTSOUND`（`long(string)`）—— 式中函数。实参经新注入的
  `Services::evaluate`（解析表 `expressionAst` 带类型上下文）求值，
  文件按 EE `Program.SoundDir`（`<游戏目录>/sound/`）解析；音量 `Math.Clamp(0,100)`；
  `PLAYSOUND` 全忙时用 0 号（对齐 EE `if (i >= sound.Length) i = 0`）。

回归：`test_audio_pipeline`（管线布局 / BGM 不重启 / SE 全忙用 0 号 / 重复次数 /
音量夹取 / 扩展登记数量与命令）。

## 底部「红绿灯」状态条（QML · 2026-10）

`src/qml/Main.qml` 的底部状态条改为**最小高度、悬浮在内容之上**的一排小圆点
（原 `footer: ToolBar` 占布局、挤压控制台）。每个灯一个含义，悬浮（Hover）弹出
ToolTip 说明：

| 灯 | 含义 | 亮起条件 |
|----|------|----------|
| 红 | 错误 | `eraEngine.hasError`（最近一次执行出错） |
| 黄 | 载入 | 脚本装载中 |
| 绿 | 等待输入 | `console.waitingInput` |
| 蓝 | 音频 | `audio.activeChannels > 0` |
| 橙 | 装载告警 | `parseWarnings()` 非空 |

## 音频·图片随机生成测试（组 30 · 2026-10）

`test/example/ERB/30_ASSET_GEN.ERB`：**用命令随机生成素材**来测音频与图片。

* **图片**（`@TEST_IMAGE_GEN`）：循环 8 次，每次随机尺寸（宽 12..71 / 高 8..47）、
  随机不透明色、随机像素点；依次跑
  `GCREATE → GCLEAR → GGETCOLOR → GSETCOLOR → GGETCOLOR → GSETBRUSH →
  GFILLRECTANGLE → GGETCOLOR → SPRITECREATE → SPRITECREATED →
  SPRITEWIDTH/HEIGHT → SPRITEGETCOLOR → HTML_PRINT <img> → GDISPOSE`。
  期望值就用同一批随机变量，所以**任何随机结果下断言都必须成立**
  （不依赖固定种子）。
* **音频**（`@TEST_AUDIO_GEN`）：随机名字/音量/重复次数驱动 EE 命令
  `PLAYBGM / PLAYSOUND "se", N / SETBGMVOLUME / SETSOUNDVOLUME / STOPBGM /
  STOPSOUND`，并用 `EXISTSOUND` 校验资源解析（存在 1 / 不存在 0）。
  素材 `test/example/sound/*.wav` 由脚本随机合成（8kHz 单声道 8bit，
  几百字节的极小文件）。

写这个测试时对照 EE（`emuera.em`）发现并修好了 3 个图片命令缺陷
（eraTW 未用到，但语义确为错）：

| 命令 | 原实现 | 修正（对齐 C#） |
| --- | --- | --- |
| `GGETCOLOR` | `(c<<24)\|r<<16\|g<<8\|b`（高位重复左移 → 垃圾值） | `c.ToArgb() & 0xFFFFFFFF`（ARGB 无符号 32 位） |
| `GSETBRUSH` | `QColor(int)`（当 RGB，丢 alpha） | ARGB（`Color.FromArgb` 同义） |
| `GFILLRECTANGLE` | 把第 2 实参当颜色、矩形整体后移一位 | `GFILLRECTANGLE id, x, y, w, h`（**无颜色实参**），用 `GSETBRUSH` 的画刷填充 |

运行：`./test/run_example.sh 30`（或 `--script 30`）；已含在组 0 全自动里。

## 字符串 `=` 赋值的「带引号字面量」（2026-10 修正）

写组 30 时踩到一个坑：`NAME = @"rand_sprite_%I%"` 得到的是 **`@"rand_sprite_0"`**
（`@"` 与引号都留在了值里），于是精灵名/图片 `src` 都带引号。

**语义**（对齐 eraTW / eraMegaten 的实际写法 + 文档）：

| 写法 | 结果 | 说明 |
| --- | --- | --- |
| `X = abc` | `abc` | 裸文本（格式串字面量） |
| `X = %V%` | V 的值 | 格式串展开 |
| `X = "abc"` | `abc` | **`"…"` 是字符串字面量，引号是定界符** |
| `X = @"abc%V%"` | `abc<V>` | **`@"…"` 是格式化字符串字面量：剥定界符 + 展开** |
| `X = "a" + TOSTR(N)` | 拼接结果 | 字符串表达式 |

即：**右值是带引号的字面量（`"…"` / `@"…"`）时，引号必须剥掉** —— 这一点上
`"…"` 与 `@"…"` 必须一致（否则同一变量用两种写法会得到不同结果）。
eraTW 同一个 `SELECTCASE` 里 `LOCALS = 倒錯的`（裸）、`LOCALS = "Ｃ感度"`（引号）、
`LOCALS = @"[目瞳:…]"`（`@"`）三种写法混用，同走一条显示路径；
eraMegaten 的 `BTL_KOJO_RESULTS:0` 同样混用 —— 只有都剥引号才自洽。

原实现只认 `"` 开头，`= @"…"` 落到**格式化串**路径 → `@"` + 引号原样落进变量
（eraTW `LOCALS = @"[目瞳:…]"` 的精灵名会带 `@"`）。

修复：`execution_engine.cpp::handleStringAssignment` 与
`era_parse_table.cpp::applyStringAssignments` 把 `@"` 开头与 `"` 开头同等对待
（都按**表达式**建节点，`@"…"` 由表达式词法解析成格式化字符串字面量）。

回归：`test_statements`（§10 `"…"` / `@"…"` / 裸格式串三者）、组 30（精灵名改用
`NAME = @"rand_sprite_%I%"`，图片 `src` 现在无引号）。

## GCLEAR 6 参（EM 私家版拡張 · 2026-10）

对齐 EE（`emuera.em` `Creator.Method.cs`）`GraphicsClearMethod` 的
`argumentTypeArrayEx` 2/6 参：GCLEAR 有两个形态（**同一个方法**）。

```erb
GCLEAR id, cARGB                 ; 全图清除（核心 2 参形态，行为不变）
GCLEAR id, cARGB, x, y, w, h     ; 只清除该矩形（SetClip + Clear + ResetClip）
```

实现：`ExtensionRegistry::regCoreArgRange(name, min, max)` 只**放宽核心命令的
实参个数区间**（核心命令不能被扩展覆盖 —— 注册类 fail-fast 拒绝；first-wins 同名
拒绝），对齐 C# 给同一个方法补第二个实参形态；校验在
`validateBuiltinCall` / `builtinFunctionArgRange` 与原生声明合并（只放大不缩小）；
6 参求值在核心 `BuiltinOp::GClear` 分派处（`GraphicsStore::gFillRectangle` 的
`QPainter::fillRect` 自带裁剪，等价 SetClip+Clear+ResetClip）。
登记住 `ee_extension.cpp`（`registerEeGraphics`）。

回归：`test_extension_registry`（§9 2..6 参 / 2 参不变 / 7 参报错 /
非核心拒绝 / first-wins）、组 30（GCLEAR 6 参矩形还原底色、矩形外不变）。

## eraTW「角色移動処理」无法退出：`END` 被当成指令（2026-10 修正）

现象（eraTW 运行日志）：

```
[未完成] 指令 "END" 在运行期被忽略。行: "…/MOVEMENT_キャラ移動処理.ERB:25:1" 原文: "END = 0"
[exec] 循环迭代异常偏多: "MOVEMENT_キャラ移動処理" 行 113 种类 2 … 已迭代 100000
```

根因：`ast_builder.cpp` 的「已知指令名」清单里列了 `"END"`，而**C# Emuera 的函数表
里没有名为 END 的指令**（`BuiltInFunctionCode.cs` 只有 `ENDIF/ENDSELECT/ENDDATA/
ENDLIST/ENDCATCH/ENDFUNC/ENDNOSKIP`）。eraTW 在 `@角色移動処理` 里用

```erb
#DIM END            ; ← 私有变量
VARSET LOCAL
END = 0             ; ← 被当指令吞掉（"= 0" 丢弃）
SIF !AT_HOME(ARG) / 行動不能 / 仕事中 …
	END = 1
IF END … RETURN     ; ← 恒假 → 早退失效
```

于是本该跳过的角色也进入移动逻辑，`WHILE` 里 `G_POINT`/引力点 的路由找不到出口
→ 单循环跑到十万次。

修复：从清单里删掉 `"END"`（保留 `QUIT` 等真指令）。`END = 0` 恢复为**赋值**；
`ENDIF/ENDSELECT/…` 不受影响（另有断言）。

回归：`test_argument_types`（`static_assert(findInstructionSpec("END") == nullptr)`
+ `END = 0` 解析为 `=`）、组 33（`END` 作早退标志 / 循环退出标志 / WHILE 计数器；
反证：把 `"END"` 加回清单，组 33 立刻 4 条 FAIL，含
「`END` 作为循环退出标志（赋值被吞则跑满 1000 次）: got=1000 want=3」）。

## `<img>` 只给 height 的尺寸（对齐 C# ConsoleImagePart · 2026-10 修正）

C# `GameView/ConsoleImagePart.cs` 的度量：

```
height = raw_height==0 ? FontSize : FontSize*raw_height/100
Width  = raw_width==0  ? DestBaseSize.Width*height/DestBaseSize.Height
                       : FontSize*raw_width/100
top    = raw_ypos * FontSize / 100
```

引擎此前把「width<=0 **或** height<=0」都当成「按资源固有像素排版」，于是 eraTW
主立絵那条只给 height 的写法

```erb
HTML_PRINT @"<nobr><img src='%sRes_Name%' height='{iFont_Hei_mag}'>"   ; IMAGE.ERB:311
```

的 `height` 被整体丢弃 → 立絵永远画成资源原始像素，
`GETCONFIG` 里的「画像サイズ 拡大/縮小」（`iSize`）**完全不起作用**；
立絵高度也不再跨 10 行（`iSize=1000` → 200px），后续 `画像枠`/`時間停止`
的负 `ypos` 叠层与 `<br>` 行数全部错位。

修复：
* `console_backend.cpp`：只有 **两个宽高都没写** 时才按固有像素排版（eraTW 标题
  = 35 张 1041×16 的条图，保持原行为）；只写 height 的一律保留。
* `console_types.h`：`ConsoleSpan` 新增 `imageIntrinsic`（生成 span 时查一次固有
  尺寸，避免排版期反复解码）。
* `console_layout.cpp::measurePart`：`raw_width==0` 且给了 height 时，宽度按
  `固有宽*heightPx/固有高` 补（纵横比），与 C# 完全一致。
* `resource_image_provider.cpp`：`loadResourceImage` 的直接文件分支改走
  `loadImageFile`（与图集分支一致），Qt 解不了的 ダミー.webp 也按文件头回退。

```erb
<img src='wide' height='1000'><br>    ; 60×20 的资源，FontSize=16 / 行高 20
;   height = 16*1000/100 = 160px（8 行）
;   width  = 60*160/20 = 480px（60 列）  ← 旧实现得到 60×20（固有像素）
```

回归：`test_image_layout`（新，6 组断言：都不写 / 只 height / 只 width / 都写 /
ypos）、`test_print_template`（`<img width='200'>` 不变）、QML 侧
`tst_console.qml::test_imageLayer`（`source == image://emuera/<名>`、
`fillMode`、非 ASCII 名 `encodeURIComponent`）。

QML/C++ 图像绘制对照 Qt 文档（6.12）复查结果：
* `requestImage` 的 `size` 必须回**原图**尺寸 —— 引擎已在缩放**之前**写 `*size`（✔）。
* provider 名的 `image:` URL 大小写：provider 标识不敏感、id 其余保留 —— 引擎用
  `encodeURIComponent` + `normalizeId`（`QUrl::fromPercentEncoding`）双向对齐（✔）。
* provider 返回的图**自动进 QML 图缓存**；eraTW 的精灵名是「内容寻址」
  （服装/表情/差分都在名字里，`@リソース登録` 同名拒绝重建），所以不需要
  `cache:false`/nonce（已核查）。
* `fillMode` 用 `PreserveAspectFit`（非 QML 默认 `Stretch`）：区块尺寸是网格量化
  值，与 C# 的像素精确 destRect 有半格误差，`Stretch` 会把它放大成拉伸
  （eraTW 标题条图会从 16px 被拉到行高）；`PreserveAspectFit` 视觉等价。
* 加 `retainWhileLoading: true`：`asynchronous` 下 source 变化默认先清空旧图，
  立絵/表情切换会闪。

## GUI 端到端 + Qt 日志体检（D-Bus 驱动 · 2026-10）

GUI（`appemuera`）现在可以完全用 D-Bus 控制跑同一套测试组，不用手点：

```bash
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.ping
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.openDirectory $PWD/test/example
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.resetPerf
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.sendInput 0
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.perf          # 区块构建计数/耗时
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.dumpScreen 20 # 屏幕尾部
```

`openDirectory` 是本次新增（对齐「文件 > 打开目录…」）：无参启动的 GUI 也能被
指到任意游戏目录；**同一个目录会 `reload()`**（脚本跑完 `QUIT` 后重跑）。
配合 `state`（`waitingInput/inputKind`）能在等待输入时自动喂 `sendInput 0` /
`sendAnyKey`，一次全组跑完 ≈ **0.4 s**（Debug 构建，`blockBuildMs` 合计 0）。

Qt 日志体检（`qt-creator` 的 Application Output）修了两处：

| 现象 | 根因 | 处理 |
| --- | --- | --- |
| `QML QQuickImage: Failed to get image from provider: image://emuera/0` ×51 | `PRINT_IMG` 一律走**整数上下文** `evaluate()`：字符串变量/裸名 → `0`。C# 的 PRINT_IMG 是 `FunctionArgType.STR_EXPRESSION`，取值 `func.Argument.IsConst ? ConstStr : Term.GetStrValue(exm)` | `execution_engine.cpp` 改走 `evaluateStr()`（字符串求值）；回归 `test_statements`「PRINT_IMG \<字符串变量\> 取变量内容」。测试夹具 `14_MISC.ERB` 的资源名加引号 |
| `[exec] CALL …` 占日志 **70%**（718/1030 行） | `script_runner.cpp` / `system_state_machine.cpp` 用**无条件** `qDebug()`；每 CALL 一次，Debug 下还要经 Qt Creator 调试连接 | 改 `qCDebug(eraTrace)`（默认关，`QT_LOGGING_RULES="era.trace.debug=true"` 或 D-Bus `setLoggingRules` 打开）。全组日志 1030 → **454** 行 |

日志里仍有（已知、非缺陷）：

* `[未完成] "指令" … 在运行期被忽略` ×21 —— 解析得到、运行期未实现：
  `ASSERT BAR BARL CALLEVENT CLEARTEXTBOX DATA ENDDATA HTML_TAGSPLIT LOADVAR
   PRINT_ABL/EXP/ITEM/MARK/PALAM/SHOPITEM/TALENT SAVEVAR SETCOLORBYNAME
   SORTCHARA STRDATA UPCHECK`（`PRINTDATA/STRDATA` 族只有 `printDataFormLine`
   helper，没有接进语句分派 —— eraTW `TW_TIPS` 会显示空）。
* `image://emuera/no_such_image_resource` —— `14_MISC` **故意**用一个不存在的
  资源验证 AltText 回退，属预期噪声。
* `qt.multimedia.ffmpeg … nonfree/unredistributable`、`Could not open media`、
  `QSoundEffect: Error decoding source file://0` —— 组 30 故意播不存在的音频。
* `kf.iconthemes: Icon theme "…" not found` —— KDE 主题缺失，无害。

## 滚动性能与「[未完成] 指令」（2026-10 · 第二批）

### 滚动卡顿：C++ 重排 → QML 整屏重建

`Console.qml` 的三个 `Instantiator` 以 `backend.textBlocks / imageBlocks / shapeBlocks`
（`NOTIFY windowChanged`）为模型。**任何一次滚动**都会让 C++ 重算窗口并重发这三个
列表，于是整屏区块对象被销毁重建；每个文本区块内部还按**字符**建 `Repeater`
（一屏 80×38 ≈ 数千个 `QQuickText`）。触摸板一秒上百个 wheel 事件 = 每秒上百次
全屏重建 —— 这就是「滑动很卡」。（`perf` 显示 C++ 侧 `blockBuildMs` 恒为 0：
慢的完全不是布局计算，而是 QML 对象创建。）

两处修正（都不改视觉语义）：

| 改动 | 效果 |
| --- | --- |
| `ConsoleBlock.qml`：文本按**等宽段**渲染（`glyphRuns`：相邻同格宽的字符合成一个 `Text`） | 半角行 1 个 Text、全角行 1 个 Text、混排 1~3 个；对象数降 1~2 个数量级。缩放仍按「段宽 = 字数 × 格宽」，等价于逐字缩放且不会字间漂移 |
| `Console.qml`：`scrollByLines()` + `Timer` 合并滚动 | 一个渲染帧最多提交一次（Qt 文档的「合并更新到渲染帧」）；此前每事件一次全量重排 |

回归：`tst_console.qml` 新增 `test_glyphRunsAreBatched`（10 半角 = 1 段 / 5 全角 = 1 段 /
`AあB` = 3 段，且段宽之和 == 区块宽）；`test_gridGlyphBounds` / `test_buttonSpanIsGridText`
的不变式从「每字一个对象」改为「每字占一格、总宽 == 区块宽」（锁布局而非实现）。

### `[未完成] … 在运行期被忽略`：21 → 14

解析得到、运行期没有分支的指令会在第一次执行时留痕（`reportUnfinished`）。本批补齐 7 个：

| 指令 | 语义（对齐 C#） | 验证 |
| --- | --- | --- |
| `SETCOLORBYNAME <色名>` | `Color.FromName`；无效/透明 -> 报错（不静默） | `test_statements`（`consoleColor` 收到 "RED"） |
| `BAR` / `BARL <值>,<最大>,<长度>` | `ExpressionMediator.CreateBar`：`[` + 实心×n + 空心×(len-n) + `]`，`BARL` 再换行；与 `BARSTR()` 共用 `ExpressionEvaluator::createBar()` | `test_statements`（`[*****.....]`、换行标志、max=0 空串） |
| `STRDATA <字符串变量>` | PRINTDATA 的**不显示**版：随机选段后把文本写进变量 | `02_PRINT.ERB`（`S == 随机串1/2`） |
| `DATA` / `ENDDATA` / `DATALIST` / `ENDLIST` / `DATAFORM` | 段的成员行，宿主已整段跳过；单独落到分发时**静默忽略**（此前误报 [未完成]） | 日志（`02_PRINT` 不再报 DATA/ENDDATA） |
| `CLEARTEXTBOX` | 清空输入栏：`ExecutionEngine::clearTextBox` → `ConsoleBackend::clearTextBoxRequested` → QML 清 `inputField` | `test_statements`（信号计数） |

剩余 14 个（eraTW 只用到其中 2 个，见下）：`ASSERT CALLEVENT HTML_TAGSPLIT LOADVAR SAVEVAR
SORTCHARA UPCHECK PRINT_ABL PRINT_EXP PRINT_ITEM PRINT_MARK PRINT_PALAM PRINT_SHOPITEM
PRINT_TALENT`。
`grep` 统计 eraTW 的使用：**`HTML_TAGSPLIT`（2 文件）、`PRINT_MARK`（1 文件）**，
其余只出现在本仓库测试夹具里 —— 按「eraTW 真的会跑」排序，这两个优先。

## 渲染回归修复（f64a919「修复渲染过慢」）与每帧抓取

`f64a919` 把三个 `textBlocks/imageBlocks/shapeBlocks` 列表改成一个增量
`ConsoleBlockModel`（`QAbstractListModel` + 按行区块缓存）。它引入两处回归：

| 回归 | 根因 | 修复 | 回归测试 |
| --- | --- | --- | --- |
| eraTW `CLEARLINE→重打印` 复用同一批**绝对行号**（如 `OPTION.ERB` 的 `CLEARLINE 10+LOCAL:1` 后 `RESTART`），点按钮后画面不刷新、按钮世代过期失效 | 模型只按「行号是否还在窗口里」判断复用，行号复用时不发任何信号 | 按**行内容版本**判定：`ConsoleBackend::lineBlocks(abs,&ver)` 回填版本；版本变了 → **行程不变则原地改写 + 一条有界 `dataChanged(起始行, 终止行)`**（Qt 文档：现有条目数据变化用 dataChanged，视图只重绑区间内委托，**不销毁/重建、行数不变、滚动不受扰动**——绝不用 remove+insert 表达内容变化，否则历史委托被拆）；行程变了才摘除重插。`clearLines`/容量裁剪/字号行高变化都会使版本失效 | `test_console_backend`「CLEARLINE 重印复用行号 / 容量裁剪 / 未提交行 / 整表重摊平也不拆历史（纯 dataChanged）」 |
| `RESETDATA` 后 `ADDCHARA`/`LOADCHARA` 的角色 NAME/ABL 全空（组 9 `LOADCHARA 后 NAME`、组 28 `ABL:技巧`） | `resetForNewGame()` 把存放 CSV 模板默认值的角色容器 `clear()` 了 | 改为从 CSV 模板快照恢复（`m_charaIntVars = m_csvIntVars` …） | `test/example/ERB/08`+`09`+`28` 全量回归 |

实测（eraTW `1,0,400` 后点 [19]）：`modelSyncs=1 modelUpdates=42 modelInserts=3
modelRemoves=1 modelResets=0` —— 42 行选项全部原地 `dataChanged` 刷新，历史行零拆除；
像素级 diff 确认 [19] 行（是→否）与世代切换的按钮带重绘。

### 每帧抓取渲染（GUI）

桌面截屏抓不到 QML 合成内容；渲染自检必须走 `Item.grabToImage`（Qt 文档：
异步把 Item 子树渲染进离屏，回调里 `ItemGrabResult.saveToFile` 落盘）。
D-Bus 提供：

```bash
# 引擎每产生一次新画面（ConsoleBackend::windowChanged）抓一张 PNG
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.startFrameCapture /tmp/f_ 0
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.stopFrameCapture
```

`startFrameCapture(prefix, limit)`：`limit<=0` 不限（靠 stop 停），存成
`<prefix>00000.png …`。一键脚本（打开目录 + 自动喂输入 + 逐帧落盘）：

```bash
./test/run_gui_frames.sh test/example /tmp/frames "0,x,x"
```

「点按钮后画面是否刷新」用帧序列直接对比：点击前后各一帧，按钮所在行像素应变化。

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

## 文档语义冒烟（组 35，依据 `data/` 的 md）

```bash
python3 test/gen_doc_smoke.py       # 合成草稿 + 渲染 example/ERB/35_DOC_SMOKE.ERB
python3 test/merge_doc_smoke.py     # 合并复核结果回 data/doc_smoke.tsv（--write 生效）
python3 test/check_doc_smoke.py     # 静态校验（不执行）
./test/run_example.sh 35            # 单跑（需注入输入，见下）
```

* 语义来源是 `data/commands/*.md` 与 `data/functions/*.md`（每条命令/函数的签名、用法示例、
  副作用与实现现状），**不需要读 C# 源码**；
* `data/doc_smoke.tsv` 是唯一事实来源，第 6 列写明该条需要 runner 注入的输入；
* `mode=skip` 的条目在 ERB 里留成 `; SKIP <名字> —— <原因>`，覆盖率仍可见。

命令清单来自：

* **Emuera 原版**：`Emuera/GameProc/Function/BuiltInFunctionCode.cs`（`enum FunctionCode`）
  与 `Emuera/GameData/Function/Creator.cs`（`methodList`）；
* **EmueraEE 扩展**：EE 发行版自带文档（`eraTW/README集/EmueraEE Readme/`）里的
  新增命令（EE 未附带 C# 源码，清单见 `export_command_tables.py` 的 `EE_EXTENSIONS`）。

当前覆盖（详见 `data/coverage_report.txt`）：原版命令 199 条、式中函数 163 条、
EE 扩展 56 条，**全部 0 未覆盖**。少量命令刻意不由自动段执行（声音/文本框/多列/
内存等需要运行环境，或会改流程），报告中逐条给出原因。

## 相关

* 编译器/引擎回归：`cd build && ctest`（**30/30 通过**）。
* 本目录的改动同时记录了若干引擎修复：`CHKDATA` 返回值（EraDataState）、
  `RESETDATA` 清空角色、`RESETGLOBAL` 保留函数私有变量、`QUIT` 结束程序、
  `SAVETEXT/LOADTEXT` 的 `txt{nn}.txt` 语义、`CHKFONT` 无 GUI 时的崩溃等。
