# 移植差异 · 命令（指令）

> 由 `test/data/commands/*.md` 的「备注」迁出（2026-10）。规则见 [`README.md`](README.md)。

## 当前注册状态（2026-10-08 静态核对）

原迁移记录把 DT_COLUMN_OPTIONS、HTML 浮岛、BREAKBUTTON 和 COLUMN 裸命令统称为
“未实现、unknown-instruction”，现按实际注册校正，并汇总已核对的 CALLSHARP。
“实现入口已存在”表示已找到注册及处理代码，不表示与 C# 完全语义等价；
“登记桩”表示名字已识别但没有该功能的处理回调；“未找到”仅指核对范围内未见注册/入口。
本轮只读核对 `src/eraengine`，没有运行测试。

| 命令 | 当前状态 | 注册与实现依据 |
| --- | --- | --- |
| `DT_COLUMN_OPTIONS` | 实现入口已存在（语句） | `GameProc/fork_datatable.cpp` 的 `registerDataTableExtensions` 用 `ext.reg(name, callback)` 解析 `DEFAULT = 值` 并设置列默认值 |
| `HTML_PRINT_ISLAND` | 登记桩 | `GameProc/ee_extension.cpp` 的 `kStubs`，经 `ext.reg(name)` 登记 |
| `HTML_PRINT_ISLAND_CLEAR` | 登记桩 | 同上 |
| `BREAKBUTTON` | 登记桩 | 同上 |
| `CALLSHARP` | 登记桩 | `GameProc/fork_extension.cpp` 的 `registerForkExtensions` 仅 `ext.reg(name)`；取舍见下文 |
| `COLUMN*` 系列 11 条（逐名见下） | 登记桩（裸命令形态） | `GameProc/ee_extension.cpp` 的 `kStubs`；`CALL` 形态调用游戏附带的 ERB 库，不能用裸命令桩判定库的支持状态 |

`COLUMN*` 已逐名核对：`COLUMNBGCOLOR`、`COLUMNCLEAR`、`COLUMNCOLOR`、`COLUMNCREATE`、
`COLUMNDIRECTION`、`COLUMNMOVE`、`COLUMNPRINT`、`COLUMNPRINTL`、`COLUMNPRINTW`、
`COLUMNRESIZE`、`COLUMNWAIT`。原位置为 `test/data/commands/<NAME>.md`。

`GameProc/extension_registry.h` 中 `reg(name)` 将名字写入桩表并调用
`AstBuilder::registerExtensionStatement`，运行期留痕跳过，因此上述桩不应记为未知指令。
构造函数调用 `registerEeExtensions` 和 `registerForkExtensions`，后者转交 DataTable 注册。

## PRINT 解析入口与行为差异

### PRINT 族后缀（`PRINT.md`）

本移植另有 C++/Qt 移植 `src/eraengine`：PRINT 族后缀解析在
`GameData/ast/ast_builder.cpp`（`AstBuilder::printInfo`）复刻了 C# `PRINT_Instruction`
构造函数的同一套顺序，但**字符集里不含 `N`**（该表的 `"VSLWCKDFORM"`），
以下 5 个名字未找到受支持的 PRINT 解析入口：
`PRINTN`、`PRINTVN`、`PRINTSN`、`PRINTFORMN`、`PRINTFORMSN`。

### PRINTSINGLE 族（`PRINTSINGLE.md`）

`PRINTSINGLE` 实现入口已存在：`GameData/ast/ast_builder.cpp` 识别前缀后继续解析后缀，
无参数类型后缀时采用普通 `Literal` 打印；
其 PRINT 族执行通路 `ExecutionEngine::handlePrintInstruction`
（`GameProc/execution_engine.cpp`）**没有 `PRINT_SINGLE` 分支** ——
即目前不做「不折行单行显示」处理。
历史迁入记录曾以 `notifyReuseLastLine` 对照 C# 的 `REUSELASTLINE` / `PrintTemporaryLine`；
这不是 PRINTSINGLE 专用实现的证据，本轮未在上述执行文件找到该旧入口名。

## CALLSHARP

C# 语义见 [CALLSHARP](../data/commands/CALLSHARP.md)。2026-10-08 回读
`src/eraengine/GameProc/fork_extension.cpp` 的 `registerForkExtensions`：
`ext.reg(QStringLiteral("CALLSHARP"))` 只登记名字；`ExtensionRegistry::reg(name)`
将名字加入桩表并通知 `AstBuilder`，属于已识别、运行期留痕跳过的桩，不能归为未知指令。
`ee_extension.cpp` 中“已真实现原生插件 ABI”的旧注释与此登记不符，应以实际登记为准。

迁入的设计取舍：Qt/QML/C++ 宿主选择不提供插件装载；C# 的 `Plugins/*.dll` 是托管程序集，
C++ 宿主若要执行它们需额外承载 .NET/CLR，直接改为原生共享库 ABI 不能兼容存量插件。
跨平台装载、Android 打包及运行时整合会引入额外维护成本。
这是当前项目的取舍，不能表述为“跨平台或 C++ 在技术上绝不可能实现”。
C# 的 `PluginManager.LoadPlugins` 使用 `Assembly.LoadFrom` 并查找 `PluginManifest`，
原语义中的变量实参回写也不是只登记名字就能实现的。

原说明还以 C# 的 `EE_CALLSHARP注意` 标记及样例游戏无调用作为取舍背景；
“eraTW/eraMegaten 零使用”属于原记录，本轮未重新扫描全部游戏，不作为语义依据。
原覆盖记录为 `doc_smoke.tsv` 中 `mode=skip`、组 39 覆盖其余 61 条且不含 CALLSHARP；
此记录是测试范围说明，不代表插件功能已验证，参见 [工具说明](tooling.md)。

## DT_COLUMN_OPTIONS 返回状态

当前 `fork_datatable.cpp` 的回调在表/列不存在时直接返回，未写 `RESULT=-1/0`；
成功设置默认值也未写返回状态。因此“有实现入口”不等于该返回状态已对齐。

从 [C# 缺陷说明](../data/commands/DT_COLUMN_OPTIONS.md) 迁入的建议：
若要在移植中实现表/列不存在时返回 `RESULT=-1/0` 的设计意图，需在设置状态后提前返回；
C# 参考实现缺少该返回，不能把意图误当成它已具备的行为。此处是建议，未声明本轮修改实现。
