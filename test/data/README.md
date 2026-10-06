# EraBasic 语义文档集

本目录下的 `commands/`、`functions/`、`language/` 三个子目录，收录了 Emuera（`emuera.em/Emuera`）中
**每条命令、每个式中函数、每个语言层主题**的语义文档，每条一篇 md，含基于 C# 源码的伪代码。

## 文档构成

| 目录 | 文件数 | 内容 | 命名 |
|---|---|---|---|
| `commands/` | 276 | 引擎命令（语句） | `<命令名>.md`，如 `PRINT.md`、`ADDCHARA.md` |
| `functions/` | 163 | 式中函数（表达式内调用的函数） | `<函数名>.md`，如 `MAX.md`、`STRFIND.md` |
| `language/` | 12 | 语言层主题（跨命令的语法与机制） | 中文主题名，如 `变量.md`、`预处理与定义.md` |

合计 451 篇、约 2.8 万行、1.6 MB。命令列表覆盖 `BuiltInFunctionCode.cs` 枚举的**全部 304 个成员**
（PRINT / PRINTSINGLE 族的 69 个后缀变体由 `PRINT.md`、`PRINTSINGLE.md` 两个基名文档统一覆盖，
其余每个成员一篇）。

### 每篇命令/函数文档的结构

```markdown
# <名字>
- **类别**：命令 / EE 扩展命令 / 式中函数
- **签名**：<全部用法，一种一行>
- **文档来源**：<ecd / Era-Chinese-Documentation 中收录该条目的页面小节；未收录则注明>
## 语义          ← 作用、参数、返回值、副作用、限制、错误行为
## 用法          ← 每种签名一个小节 + 可运行 erb 示例
## 源码实现      ← 注册处与实现类的真实 文件:行号 + 伪代码
## 备注          ← 文档与源码的差异、本仓库未实现等
```

### 语言层主题清单

| 文件 | 覆盖内容 |
|---|---|
| `变量.md` | 内置变量全集、角色变量、多维数组、局部变量、伪变量、CSV 变量、保存范围、自定义变量 |
| `表达式.md` | 值类型、FORM 语法、类型转换、表达式内可用的东西 |
| `运算符.md` | 运算符全集与真实优先级、短路求值、自定义表达式内函数 |
| `预处理与定义.md` | `#DEFINE`/`#DIM`/`#FUNCTION` 等预处理指令、ERH 头文件、宏、装载流程 |
| `语句与复合语句.md` | 语句分类、IF/SELECTCASE/循环/DATA 块等复合语法与嵌套规则 |
| `ERB结构与函数.md` | 文件结构、事件函数、BEGIN 流程、调用模型 |
| `内置流程.md` | 系统函数调用约定、标题/商店/调教各阶段内置处理顺序 |
| `CSV文件.md` | 角色 CSV 全部列、VariableSize.csv、`_Replace.csv` 全部条目 |
| `配置文件.md` | emuera.config 全部配置项与默认值 |
| `HTML显示.md` | HTML_PRINT 标签子集、属性、事件绑定 |
| `调试与错误.md` | 调试模式、调试命令、错误分类与消息索引 |
| `兼容性与版本.md` | 与 Eramaker/旧版的差异、版本特性时间线、术语表 |

## 相关文档

- [`源码树对照.md`](源码树对照.md) —— 本仓库内四套 Emuera 实现/构建产物（文档依据树、eraTW 随附 exe、
  私家改造版 v11 基线、仓库根经典树）的身份、版本谱系与**命令集差异**。
  写 ERB 前建议先看它的第三、四节：文档树（EMv18/EEv56）与 eraTW 运行时（EMv17/EEv41）之间
  有 15 个命令不通用。

## 语义来源与权威性

每条文档的语义按以下优先级确定，冲突时在「备注」中双向记录：

1. **C# 源码**（权威实现）：`emuera.em/Emuera/`
   - 命令：注册在 `Runtime/Script/Statements/FunctionIdentifier.cs`，实现在
     `Runtime/Script/Statements/Instraction.Child.cs`（`*_Instruction` 类）或
     `Runtime/Script/Process.ScriptProc.cs`（switch 分发与流程控制）
   - 函数：注册在 `Runtime/Script/Statements/Function/Creator.cs`（`methodList`），实现在
     `Runtime/Script/Statements/Function/Creator.Method.cs`（`*Method` 类）
2. **ecd 文档**（`ecd/docs/`，内容较全，含每条命令的详解与签名）
3. **Era-Chinese-Documentation**（`Era-Chinese-Documentation/docs/`，较旧较小，用于交叉核对）

两套站点文档已提取为纯文本，放在 `_extracted/ecd/`、`_extracted/zh/`，便于检索比对。

## 覆盖率

| 类别 | 权威来源 | 文档数 | 覆盖 |
|---|---|---|---|
| `BuiltInFunctionCode.cs` 枚举成员 | 枚举 + `FunctionIdentifier.cs` 注册 | 304 | 100% |
| 式中函数 | `Creator.cs` methodList | 163 | 100% |
| 语言层主题 | 两套站点文档全部参考页 | 12 | — |

权威命令表由 `test/export_command_tables.py` 导出（`emuera_standard_cmds.txt`、
`emuera_ee_cmds.txt`、`emuera_standard_funcs.txt`）。当前导出结果：

| 清单 | 条数 | 含义 |
|---|---|---|
| `emuera_standard_cmds.txt` | 263 | 原版（经典布局基线树）枚举 − 内部值；**含 PRINT 族全部后缀变体** |
| `emuera_standard_funcs.txt` | 163 | 各棵树的 `Creator.cs` methodList 并集 |
| `emuera_ee_cmds.txt` | 59 | (超集树 `emuera.em/Emuera` 枚举 − 原版枚举) ∪ EE 文档独有名字 |

导出脚本自带自检：两清单的并集必须覆盖全部树的枚举成员且彼此不相交，运行时打印
`[OK] 枚举成员 302 条全部收录，两清单无重复`。

### 导出脚本的两处历史缺陷（已修）

写入本套文档的核对过程发现 `test/export_command_tables.py` 有两个缺陷，**均已修正**：

1. **漏收 88 个枚举成员**——原因是它只读经典布局的 `Emuera/` 树，而重构版
   `emuera.em/Emuera` 是超集，多出 69 个 PRINT / PRINTSINGLE 族后缀变体与 19 个独立命令
   （`SET`、`CALLSHARP`、`SETBGIMAGE`、`CLEARBGIMAGE`、`REMOVEBGIMAGE`、`REF`、`REFBYNAME`、
   `TOOLTIP_SETFONT`、`TOOLTIP_SETFONTSIZE`、`TOOLTIP_CUSTOM`、`TOOLTIP_FORMAT`、`ONEBINPUT`、
   `ONEBINPUTS`、`BREAKBUTTON`、`DT_COLUMN_OPTIONS`、`VARI`、`VARS`、`HTML_PRINT_ISLAND`、
   `HTML_PRINT_ISLAND_CLEAR`）。现改为**读取全部 C# 树取并集**。
   本套文档为这 88 个名字补了 21 篇（两个 PRINT 基名文档 + 19 个独立命令）。
2. **PRINT 族过滤连基名一起跳过**——原先的过滤器把 `PRINT`、`PRINTSINGLE` 也排除了，
   导致基名不在清单里。现已取消该过滤，清单忠实反映枚举；是否逐个冒烟由
   `gen_coverage.py` 的 `PRINT_BASE` 规则决定。

相应地，`gen_coverage.py` 的 `PRINT_BASE` 补上了 `N` 后缀（超集树新增的 5 个 `*N` 变体）。
「某条目为何不自动执行」不再在 `gen_coverage.py` 里另抄一份，而是**直接读本目录
`doc_smoke.tsv` 的 mode/note**：`mode=call` 的条目 = 组35 已真实调用（无需理由），
`mode=skip` 的条目 = 仍跳过、note 即原因。要「消除」某条理由，就在复核清单/`_smoke_fix`
批处理里把它改成 `call` 并给出可执行 snippet。

### EE 清单中的伪名（已从清单移除）

早期版本的 `emuera_ee_cmds.txt` 里有 5 个名字并非真实命令，源自 Shift-JIS 编码下的文本切分；
已从清单移除，对应的说明文档保留在 `commands/` 下：

| 伪名 | 真实指向 |
|---|---|
| `FSTRJOIN` | `STRJOIN`（式中函数） |
| `FTOOLTIP_SETDURATION` | `TOOLTIP_SETDURATION` |
| `STRJOIN1` | `STRJOIN`（同上的另一种切分） |
| `TINPUTAWAIT` | 无此命令，来自「TINPUT 与 AWAIT 的挙動変更」一句 |
| `TOOLTIP_EXTENSION` | 无此命令，是 EM 文档站的参考页名 |

### 未实现 / 受限的条目

以下命令在文档/清单中存在，但本仓库（`emuera.em/Emuera`）中不可用或受配置限制，
各文档的伪代码一节均已注明实际状态：

| 条目 | 状态 |
|---|---|
| `COLUMN*` 系列 11 条（`COLUMNCREATE`/`COLUMNPRINT` 等） | 本仓库无实现（EE 发行版以 ERB 库形式提供） |
| `LCSVISASSI`、`OCLEARLINE`、`GETTEXTSIZE` | 无实现；`GETTEXTSIZE` 疑为 `GETTEXTBOX` 的文档笔误 |
| `SAVEVAR`、`LOADVAR` | 已注册，但 `DoInstruction` 直接抛 `NotImplCodeEE` |
| `REF`、`REFBYNAME` | 双重失效：实现抛 `NotImplCodeEE`，且解析侧同样不可用（1.815 起移除） |
| `CHKVARDATA`、`CHKGLOBALDATA`、`FIND_VARDATA` | 注册行被注释，未注册 |
| `VARI`、`VARS` | 已实现，但仅在 `setting.json` 的 `UseScopedVariableInstruction` 开启时注册 |
| `OUTPUTLOG` | 命令形式在文档树中停用，改为式中函数 `OUTPUTLOG(文件名{, hideInfo})` |

## 复核

因为大量命令**共用同一个 C# 实现类**（一个误读会同时污染多篇），复核没有逐篇重写，而是三层交叉检验：

1. **机器校验**（脚本）：
   - 文件覆盖 451/451；模板七节齐全；代码围栏配对；无占位符遗留。
   - **2198 处 `文件:行号` 引用**逐条验证：路径可达、行号不越界、并做「引用行内容是否真是被引用的东西」的
     内容级回读（据此发现并修正了索引来源的 off-by-one）。所有引用已全量规范化为唯一相对路径。
   - 签名令牌覆盖：命令签名对照 ecd 的 243 个 `### 名字 <签名>` 小节；函数签名对照 `Expression.md` 的
     91 条目录签名，不一致 0 处。
2. **枚举 vs 清单核对**：查出 `BuiltInFunctionCode.cs` 有 88 个枚举成员未被权威清单收录（详见上节），补写 21 篇。
3. **语义簇互检**：按「共用同一实现类」把文档分成 17 个共享簇（覆盖约 100 篇），
   每组先把共享实现读准一次，再检查每篇文档是否与实现一致、组内文档之间是否互相矛盾。

复核修正的主要问题（部分）：

| 类别 | 发现 |
|---|---|
| 批量误读 | 10 篇 TRY 系文档把「失败跳转」写成「跳到 ENDCATCH 之后」，实为跳到配对 CATCH 行（落点 = CATCH 下一行） |
| 批量误读 | `PRINTDATA` 系文档沿袭 ecd 转述的「按变量值进入指定编号」，实为随机选中后**回写**变量 |
| 批量误读 | 4 篇 LC 系文档的填充量少算 1（实为 `PrintCLength + 1`） |
| 路径错误 | 24 篇注册路径写成不存在的 `Runtime/Script/Functions/`；12 篇缺 `Statements/` 层 |
| 行号错误 | 索引 off-by-one 导致的多处枚举/注册行差 1；已重建索引并全量校正 |
| 语义错误 | `CALL` 的「字符串参数写入 `RESULTS`」无源码依据（已删）；`ADDDEFCHARA` 实际最多添加 2 个角色；`SAVEGAME` 并非「执行后不返回」 |
| 源码缺陷（如实记录） | `SETBIT` 越界抛的是 `FormatException` 而非可读提示；`GSETCOLOR`/`GGETCOLOR` 缺 `Y < 0` 检查；`ARRAYREMOVE` 字符串分支缺越界检查；`DT_COLUMN_OPTIONS` 的错误分支不可达 |

各文档「备注」节记录了文档与源码的全部已知差异，含上述缺陷与「两套中文文档均未收录」的说明。
