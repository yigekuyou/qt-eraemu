# 文档生成代理工作指南

本目录（`test/data/`）正在为每条 EraBasic 命令/函数生成一个独立的 md 语义文档。
你（代理）负责其中一批。所有材料已预提取好，按以下步骤工作。

## 材料位置

| 材料 | 路径 | 说明 |
|---|---|---|
| 源码位置索引 | `test/data/_extracted/source_index.md` | 每个名字 → 注册处/实现类/switch-case 行号 |
| 主语义文档（详解） | `test/data/_extracted/ecd/Command.md` | ecd 文档站翻译的 Emuera 命令详解，每命令一个 `### 名字 <签名>` 小节 |
| 函数目录 | `test/data/_extracted/ecd/Expression.md` | 内置函数签名与简述（`### 章节`） |
| 变量文档 | `test/data/_extracted/ecd/Variable.md`、`ecd/EraBasic_Variables.md` | 变量语义 |
| 副本语义文档 | `test/data/_extracted/zh/` | Era-Chinese-Documentation 套件（内容较旧较小，用于交叉核对） |
| C# 源码根 | `emuera.em/Emuera/` | 权威实现 |
| 命令实现 | `Runtime/Script/Statements/Instraction.Child.cs`（`*_Instruction` 类）、`Runtime/Script/Process.ScriptProc.cs`（switch 分发/流控制） | |
| 函数实现 | `Runtime/Script/Statements/Function/Creator.Method.cs`（`*Method` 类）、注册在 `Function/Creator.cs` | |
| EE 扩展命令 | 部分在本仓库不存在（索引中标注 ABSENT），这类条目仅按文档写语义，伪代码一节写「本仓库未实现」 | |

以上路径均相对 `/mnt/DATA/github/emuera/`（下文记作 `$R`）。索引中的 `文件:行号` 就是给你定位用的，写文档时要转成真实行号引用。

## 输出文件模板

每条命令写到 `$R/test/data/commands/<名字>.md`；每条函数写到 `$R/test/data/functions/<名字>.md`。
文件名 = 大写命令名 + `.md`。模板：

```markdown
# <名字>

- **类别**：命令 / EE 扩展命令 / 式中函数
- **签名**：<文档给出的完整签名，含全部复数用法，一种一行>
- **文档来源**：`ecd/docs/translation/Command.md`、`Era-Chinese-Documentation/docs/...`（写实际小节名；若某套文档未收录则注明）

## 语义

<用 2~6 句话说清这条指令做什么：作用、参数含义、返回值（函数）、副作用、
使用限制（只能在哪些上下文用）、错误行为。以 ecd 文档为准，zh 文档补充或校正。>

## 用法

### <用法 1 签名>
<参数逐个说明>
```erb
<可运行的最小示例>
```
### <用法 2 签名>   ← 有些命令有多种用法/后缀变体，必须逐一列出
...

## 源码实现（emuera.em/Emuera）

- 注册：`<文件>:<行>`
- 实现：`<文件>:<行>`（类名）

```text
<伪代码：把 C# 实现翻译成易读伪代码，保留分支语义、循环、错误抛出、
全局副作用顺序。不要逐行照抄，但语义必须等价。>
```

## 备注

<两套文档差异、文档与源码不一致处、本仓库未实现等。没有就写「无」。>
```

## 硬性要求

1. **每个名字一个文件**，一个不许漏。完工时用 `ls` 自查数量。
2. **多种用法必须全列**（如 PRINT 的 V/S/FORM/FORMS、K/D、L/W 后缀组合，SETCOLOR 两种签名等），以文档签名区为准。
3. **伪代码必须基于真实源码**：按索引行号读 C#，再翻译。禁止凭空编造；若实现是转调别的类，跟进去读。
4. 文档与源码冲突时：如实写出冲突，两边都记。
5. 全文中文（命令名、代码除外）。
6. 不要改动索引和提取材料，只写你分到的输出文件。
