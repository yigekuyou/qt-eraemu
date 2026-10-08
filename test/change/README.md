# test/change —— 本移植与原版（C#）的差异

**规则（2026-10 起）**

- `test/data/**` 只放 **C# 原版的语义与实现**（参考文档）：签名、`文档来源`、
  `Runtime/...` / `UI/...` 的源码引用、以及「文档与源码」的出入。**不写本移植**。
- 本移植（C++/Qt，`src/eraengine/**`）的一切差异 —— 实现状态（实现入口已存在 / 登记桩 / 未找到）、
  结构映射、设计取舍、与 C# 的行为出入、回归测试位置 —— 一律写在本目录。
- 按实际依据判断：描述 `src/eraengine` 的实现行为才迁入；指向本目录的导航链接可以留在参考手册。C/C++ `#define` 的语言类比、C# 源码的缺陷或版本差异不是移植现状。

> 为什么：`test/data` 是**可独立引用的参考手册**（对应 `emuera.em/Emuera` 这棵 C# 树），
> 而移植实现开发初期会剧烈变动，混在一起会让「哪句是 C# 事实、哪句是本仓库现状」不可辨。

## 目录

| 文件 | 内容 |
| --- | --- |
| `commands.md` | 命令（指令）层面：注册状态 / 差异 |
| `functions.md` | 式中函数层面：注册状态 / 差异 |
| `language/*.md` | 语言层（对应 `test/data/language/` 同名文档）的设计差异 |
| `tooling.md` | 测试工具链层面的移植相关说明（`test_cli`、`_smoke_fix` 等） |

## 边界

- C# 参考树 `emuera.em/Emuera/` 与仓库根 `Emuera/` 的差异、未注册函数、
  `NotImplCodeEE` 等事实仍在 `test/data`，不能因出现“本仓库”便迁走。
- `test/data/_extracted/` 是来源材料，保持原文。
- 工具指南与覆盖记录已迁至 [tooling.md](tooling.md) 和 [覆盖快照](coverage_report.txt)，
  原路径保留入口；TSV/TXT 夹具及测试代码不在这次文档修改范围内。
- 2026-10-08 已逐名只读核对 `commands.md`、`functions.md` 清单的核心表、扩展注册和处理入口，
  按“实现入口已存在 / 登记桩 / 未找到”校正，并区分式中函数与同名语句入口。
  此为静态源码快照，不是完整语义等价或运行测试结论；其它历史测试记录按历史标注。

## 已迁入

| 来源 | 现在位置 |
| --- | --- |
| `test/data/commands/{DT_COLUMN_OPTIONS,HTML_PRINT_ISLAND,HTML_PRINT_ISLAND_CLEAR,BREAKBUTTON,PRINT,PRINTSINGLE,CALLSHARP,COLUMN*}.md` 的「备注」 | [`commands.md`](commands.md) |
| `test/data/functions/*.md` 的「备注」（历史 44 个 `未实现` 名字，当前状态已校正） | [`functions.md`](functions.md) |
| `test/data/language/语句与复合语句.md`（命令语句表/映射陷阱） | [`language/语句与复合语句.md`](language/语句与复合语句.md) |
| `test/data/language/调试与错误.md`（本移植诊断模型） | [`language/调试与错误.md`](language/调试与错误.md) |
| `test/data/language/运算符.md`（三元/绑定力已对齐说明） | [`language/运算符.md`](language/运算符.md) |
| `test/data/language/预处理与定义.md`（宏展开防护） | [`language/预处理与定义.md`](language/预处理与定义.md) |
| `test/data/_smoke_fix/{GUIDE,INPUT_GUIDE}.md` | [工具指南](tooling.md)（原路径保留链接） |
| `test/data/coverage_report.txt` | [历史覆盖快照](coverage_report.txt) |
| `OCLEARLINE` / `LCSVISASSI` 的测试桩推测 | [工具说明](tooling.md#未确证名字的测试推测) |

迁移日期：2026-10。

本轮阅读范围、迁移清单与验证方法见 [文档整理记录](文档整理记录.md)。
