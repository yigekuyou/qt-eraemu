# 复核代理工作指南

上一阶段已为 418 条命令/函数各生成一个 md（`test/data/commands/`、`test/data/functions/`），
以及 12 个语言层主题 md（`test/data/language/`）。你现在做**独立复核**：以怀疑的眼光逐条核验，
发现错误就**直接修正文件**，并在汇报中列明改了什么。

## 材料（相对 /mnt/DATA/github/emuera/）
| 材料 | 路径 |
|---|---|
| 待复核文件清单 | `test/data/_extracted/review_batches/<批次>.txt`（每行一个相对路径，如 `commands/PRINT.md`） |
| 主语义文档 | `test/data/_extracted/ecd/`（`Command.md`、`Expression.md`、`Variable.md` 等） |
| 副本语义文档 | `test/data/_extracted/zh/` |
| 源码位置索引 | `test/data/_extracted/source_index.md` |
| C# 源码 | `emuera.em/Emuera/` |

## 每个文件必查的 6 项
1. **签名完整性**：文档给出的每种用法/后缀变体/可选参数是否都写进「签名」与「用法」？对照 ecd 文档该小节与 C# 参数构建器（`ArgumentBuilder.cs` 的 `*ArgumentBuilder`、`FunctionIdentifier.cs` 注册的 flag）确认。
2. **语义正确性**：`## 语义` 是否与 ecd 文档一致？有无遗漏关键限制（可用上下文、错误抛出、返回值）。
3. **伪代码与源码一致**：打开 `## 源码实现` 里引用的 `文件:行号`，逐条核对伪代码分支、循环、错误抛出、副作用顺序是否等价。行号必须真实（可 grep 验证）。**发现行号错误必须改正**。
4. **引用可达**：所有 `文件:行号` 引用必须用真实文件名（如 `EmueraConsole.Print.cs`、`Process.ScriptProc.cs`），不能写简称 `Print.cs`、`ScriptProc.cs`。已发现 4 处此类问题：`commands/BAR.md`、`commands/BARL.md`（Print.cs）、`commands/DOTRAIN.md`（SystemProc.cs）、`commands/CALLTRAIN.md`（ScriptProc.cs）——请自行 confirm 并修正涉及的文件。
5. **备注节有效性**：`## 备注` 中声称的「文档与源码差异」是否真实成立（去源码验证）？虚假或已过时的差异描述要删除或改写。
6. **模板完整**：类别 / 签名 / 文档来源 / 语义 / 用法 / 源码实现 / 备注 七节齐全；无占位符（TODO、待补、xxx）。

## 修正原则
- 只改**确证有误**的内容；不确定的不要动，写进汇报。
- 修正时保持文件原有结构与中文风格，改动最小化。
- 伪代码块用 ```text 围栏；erb 示例用 ```erb。
- 禁止为了「看起来更完整」而臆造文档未记载又没有源码依据的行为。

## 汇报格式
- 复核文件数 / 实际修改的文件清单（每个文件一句话说明改了什么）
- 未能确证、留待人工判断的问题清单
- 复核中发现的、之前生成代理漏报的文档-源码差异
