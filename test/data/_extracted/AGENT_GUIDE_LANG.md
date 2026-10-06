# 语言层文档生成代理指南（分组主题）

你负责一个语言层主题（不是单个命令），把成果写到
`/mnt/DATA/github/emuera/test/data/language/<主题文件名>.md`（文件名见你的任务描述）。

## 材料（相对 /mnt/DATA/github/emuera/）
- 提取文档：`test/data/_extracted/ecd/`（主，内容全）、`test/data/_extracted/zh/`（副，用于交叉核对）
- C# 源码：`emuera.em/Emuera/`
  - 装载/解析：`Runtime/Script/Loader/ErbLoader.cs`、`Runtime/Script/Parser/LogicalLineParser.cs`
  - 执行：`Runtime/Script/Process*.cs`、`Runtime/Script/Statements/`（Expression 子目录为表达式求值）
  - 变量：`Runtime/Script/Statements/Variable/`、`GameData/Variable/`
- 单条命令/函数的语义不在你的范围内——它们已按条生成于 `test/data/commands/`、`test/data/functions/`，你可以引用文件名（如「详见 commands/PRINT.md」），不要重复其细节。

## 输出模板
```markdown
# <主题>

> 来源：ecd/docs/<页面>、Era-Chinese-Documentation/docs/<页面>

## 概述
<该主题在 EraBasic 语言中的位置与作用，2~5 句>

## <分节若干，覆盖主题内全部概念>
<概念说明、语法、规则、示例（erb 代码块）、限制与错误行为>
<关键处给出对应 C# 实现的伪代码块（text 代码块），标注真实文件:行号>

## 与源码的差异/备注
<文档与实现不一致处；无则写「无」>
```

## 硬性要求
1. 全文中文，命令名/代码除外。
2. 两套文档都要查（ecd 为主，zh 交叉核对，差异要写明）。
3. 语法规则部分要给基于源码的伪代码（读 ErbLoader/Parser/相关类的真实行号）。
4. 完工后汇报：写了哪个文件、覆盖了哪些小节、两套文档差异、与源码的差异。
