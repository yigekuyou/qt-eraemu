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
| 30 | **音频·图片（用命令随机生成素材）**（`30_ASSET_GEN.ERB`） |
| 31 | **GETCONFIG/GETCONFIGS（emuera.config 取值）**（`31_GETCONFIG.ERB`） |

「全部自动运行」（`./test/run_example.sh` 无参数）依次执行：
**1–10、14、16、17、23–28、30、31 + 汇总**。

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

* 编译器/引擎回归：`cd build && ctest`（**30/30 通过**）。
* 本目录的改动同时记录了若干引擎修复：`CHKDATA` 返回值（EraDataState）、
  `RESETDATA` 清空角色、`RESETGLOBAL` 保留函数私有变量、`QUIT` 结束程序、
  `SAVETEXT/LOADTEXT` 的 `txt{nn}.txt` 语义、`CHKFONT` 无 GUI 时的崩溃等。
