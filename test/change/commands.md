# 移植差异 · 命令（指令）

> 由 `test/data/commands/*.md` 的「备注」迁出（2026-10）。规则见 [`README.md`](README.md)。

## 未实现（`src/eraengine` 回落为「未识别的指令」，解析/运行期留痕跳过）

以下命令在 C# 侧有语义（见各自的 `test/data/commands/<NAME>.md`），但本移植未实现，
在 `AstBuilder` 里按 `unknown-instruction` 诊断登记、执行期忽略：

| 命令 | 说明 | 原位置 |
| --- | --- | --- |
| `DT_COLUMN_OPTIONS` | 未实现本命令（DataTable 命令形态） | `test/data/commands/DT_COLUMN_OPTIONS.md` |
| `HTML_PRINT_ISLAND` | 未实现本命令（独立层渲染） | `test/data/commands/HTML_PRINT_ISLAND.md` |
| `HTML_PRINT_ISLAND_CLEAR` | 未实现本命令 | `test/data/commands/HTML_PRINT_ISLAND_CLEAR.md` |
| `BREAKBUTTON` | 未实现本命令（按钮世代作废） | `test/data/commands/BREAKBUTTON.md` |
| `CALLSHARP` | 未实现（需 Plugins/*.dll 的 IPluginMethod） | `test/data/commands/CALLSHARP.md` |
| `COLUMN*` 系列 11 条 | 未实现（EE 发行版以 ERB 库 `COLUMN_LIB` 提供，C# 亦无实现） | `test/data/commands/COLUMN*.md` |

## 行为差异（已实现但与 C# 有出入）

### PRINT 族后缀（`PRINT.md`）

本移植另有 C++/Qt 移植 `src/eraengine`：PRINT 族后缀解析在
`GameData/ast/ast_builder.cpp`（`AstBuilder::printInfo`）复刻了 C# `PRINT_Instruction`
构造函数的同一套顺序，但**字符集里不含 `N`**（该表的 `"VSLWCKDFORM"`），
即本移植尚未支持 `PRINTN`/`PRINTVN`/`PRINTSN`/`PRINTFORMN`/`PRINTFORMSN` 这 5 个成员。

### PRINTSINGLE 族（`PRINTSINGLE.md`）

本移植把 `PRINTSINGLE` 前缀归为普通 `Literal` 打印（`GameData/ast/ast_builder.cpp`），
其 PRINT 族执行通路 `ExecutionEngine::handlePrintInstruction`
（`GameProc/execution_engine.cpp`）**没有 `PRINT_SINGLE` 分支** ——
即目前不做「不折行单行显示」处理。
另注意：本移植的 `notifyReuseLastLine` 对应的是 C# 的 `REUSELASTLINE` / `PrintTemporaryLine`
（`temporary: true` 那一支），**不能**用于对照本族。
