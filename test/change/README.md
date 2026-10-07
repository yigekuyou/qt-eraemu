# test/change —— 本移植与原版（C#）的差异

**规则（2026-10 起）**

- `test/data/**` 只放 **C# 原版的语义与实现**（参考文档）：签名、`文档来源`、
  `Runtime/...` / `UI/...` 的源码引用、以及「文档与源码」的出入。**不写本移植**。
- 本移植（C++/Qt，`src/eraengine/**`）的一切差异 —— 实现状态（未实现 / 留痕桩）、
  结构映射、设计取舍、与 C# 的行为出入、回归测试位置 —— 一律写在本目录。
- 判定标记（出现即属本目录内容）：`src/eraengine`、`本移植`、`C++/Qt 移植`、`生态差异`。

> 为什么：`test/data` 是**可独立引用的参考手册**（对应 `emuera.em/Emuera` 这棵 C# 树），
> 而移植实现开发初期会剧烈变动，混在一起会让「哪句是 C# 事实、哪句是本仓库现状」不可辨。

## 目录

| 文件 | 内容 |
| --- | --- |
| `commands.md` | 命令（指令）层面：未实现 / 差异 |
| `functions.md` | 式中函数层面：未实现 / 差异 |
| `language/*.md` | 语言层（对应 `test/data/language/` 同名文档）的设计差异 |
| `tooling.md` | 测试工具链层面的移植相关说明（`test_cli`、`_smoke_fix` 等） |

## 边界（本目录不收什么）

- `test/data/**` 里的**工具用法**保留在原处：`test/data/_smoke_fix/**` 与 `doc_smoke.tsv`
  是本仓库的冒烟夹具与调用清单（不是 C# 参考手册），其中 `test_cli --script ...` 之类的
  运行说明属于夹具自身的使用方法，不迁出。
- `test/data/源码树对照.md`、`test/data/README.md` 里说的「本仓库」指**随仓库携带的 C# 参考树**
  （`emuera.em/Emuera`、`eraTW/` 随附 exe、补丁基线、仓库根 `Emuera/`），那是「C# 侧的事实」，
  不属于本目录。

## 已迁入

| 来源 | 现在位置 |
| --- | --- |
| `test/data/commands/{DT_COLUMN_OPTIONS,HTML_PRINT_ISLAND,HTML_PRINT_ISLAND_CLEAR,BREAKBUTTON,PRINT,PRINTSINGLE,CALLSHARP,COLUMN*}.md` 的「备注」 | [`commands.md`](commands.md) |
| `test/data/functions/*.md` 的「备注」（44 条 `未实现`） | [`functions.md`](functions.md) |
| `test/data/language/语句与复合语句.md`（命令语句表/映射陷阱） | [`language/语句与复合语句.md`](language/语句与复合语句.md) |
| `test/data/language/调试与错误.md`（本移植诊断模型） | [`language/调试与错误.md`](language/调试与错误.md) |
| `test/data/language/运算符.md`（三元/绑定力已对齐说明） | [`language/运算符.md`](language/运算符.md) |
| `test/data/language/预处理与定义.md`（宏展开防护） | [`language/预处理与定义.md`](language/预处理与定义.md) |
| `test/data/_smoke_fix/GUIDE.md`（「`src/**` 不能作为语义依据」一行） | 本文件（规则） |

迁移日期：2026-10。
