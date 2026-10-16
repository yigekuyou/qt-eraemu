# eraemu LSP / MCP 工具

AST 指 `src/eraengine/GameData/ast` 中的引擎实现，不提供单独的 AST 可执行程序。

这些工具动态链接本项目的 `eraengine`，通过共享库 `eproto` 复用同一份分析与协议实现。无需启动 QML 窗口，不执行游戏脚本。

## 构建与验证

项目要求 Qt 6.11+、C++23。工具默认启用，Android 不构建桌面 stdio 工具；可设置 `-DEMUERA_BUILD_TOOLS=OFF`。

```sh
cmake -S . -B build
cmake --build build --target eralsp eramcp -j4
cmake --build build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
cmake --install build --prefix /your/install/prefix
```

本轮在 Linux / Qt 6.11.2 上验证，Qt API 文档查询使用 qtdocs 提供的 Qt 6.12 文档。未验证 Windows/macOS；尤其引擎现有 C++ API 尚未全面添加 Windows DLL 导出声明，不能据 Linux 测试声称 Windows 可用。

## 分析方式

`ErbAnalyzer::parse(text, fileName)` 保留单文档语法分析接口。

`ErbAnalyzer::analyzeWorkspace(QMap<QString, QString>)` 接受文件名或 URI 到源码的映射：

1. 从 ERH 汇集引擎预处理宏，按确定顺序先处理 ERH，再处理 ERB。
2. 使用 `ErbPreprocessor`、`AstBuilder` 和 `EraParseTable::expressionAst()` 构造 AST。
3. 调用 `EraParseTable::markLoadComplete()`，对已构造 AST 完成跨文件声明定稿、变量类型就地定型、表达式函数绑定、参数及类型检查（`loadScript` 只登记脚本/函数/标签，收尾在装载完成时一趟完成）。
4. 输出来自引擎的结构化诊断与 AST 标签。每次分析建立新快照，避免删除文件后残留符号。

引擎修改保留了诊断的文件、物理行、列和跨度，并让不同位置的同类错误分别报告。协议返回的位置统一为 **0 起始、UTF-16 代码单元**。

### 单文档 vs 整局上下文

`source` 永远是单文档分析（工作区里只有这一份文本），跨文件定义不可见 —— 引用别处定义的
函数会报「未定义的函数」，这是**分析范围**的必然结果，不是脚本错误。

`path` 落在 `<游戏>/ERB` 目录内时自动切换为整局上下文：`ErbAnalyzer::detectWorkspaceDir`
从文件向上找最近的 `ERB` 目录（大小写不敏感，与引擎装载范围一致），`analyzeGameWorkspace`
把该目录下全部 `*.erb`/`*.erh` 一次性交给 `analyzeWorkspace`（跨文件声明、函数绑定、类型
收尾全量生效），并按「文件集 + 大小 + mtime」签名缓存，未变化时后续调用零读盘；文件遍历复用
引擎装载同一套文件 IO（`FileSystem::collectTree`/`listFilesFromTree`），LSP 与 MCP 不再各持
一份读盘实现。

eraTW（2260 文件）实测：单文档 43998 条诊断 → 整局上下文 376 条 → **现在 6 条**（下面两节
修掉的两类噪声）；eraH（899 文件）4102 → 258 → 同源收敛。整局上下文现在与 appemuera 全量
装载的告警**逐条一致**（eraTW 双方都是那 6 条 `CALL GAMEOVER`）。

导致残余噪声的三个真实缺陷（本次修复）：

1. **分析器丢作用域**：构造 AST 时用 `EraParseTable::expressionAst()`（无作用域）而不是
   `expressionAstInScope(expr, 当前函数)`。装载器走的是**带作用域**的解析，于是
   「与别处同名常量重名的函数私有变量」在 LSP/MCP 侧被折成那个常量的字面量 ——
   eraTW `COMF625/COMF448` 的 `削除行数 ++`（`CASINO_SLOT.ERB` 另有
   `#DIM CONST 削除行数 = 51`）就被判成「第一个参数需要变量节点」。
2. **陈旧错误状态**：`X ++` 的权威判定在收尾第 3 步重新解析，但那之前第 2 步
   `ArgumentParser::build` 已按构建期的临时 AST 记下一条错误；重新解析通过后没有清掉，
   语义正确的行仍带着旧错误进 `validateArguments`。现在通过时显式清 `typeOk/errMes`。
3. **`[[…]]` 换字未装载**：`CSV/_Rename.csv` 的 `[[名前]] -> 番号` 换字表在装载器里由
   `ErbLoader::tryAutoLoadRename` 装载，分析器没有这一步，于是 `[[鈴仙・優曇華院・イナバ]]`
   被当成字符串字面量 —— `勝負内衣判定([[…]])` 报「第 1 个实参需要 Int，实得字符串」、
   `1 + 2 * [[…]]` 报「二元运算类型错误」，占 eraTW 残余噪声的绝大多数。现在
   `ErbAnalyzer::readRenameMap` 按同一批候选路径（`<游戏>/CSV/_Rename.csv` 优先）装载。

### 运行期目标的静态检查（label-not-found）

C# `ErbLoader.setJumpTo`（装载第 3 趟）会在装载期把**目标静态可知**的跳转/调用解析一次，
解析不到就把该行标成 `IsError`（运行到必抛），`Config 関数が見つからない警告の扱い` 只管
**是否显示警告**、不影响该判定。本实现同口径，收尾新增步骤 4（`resolveStaticTargets`）：

| 指令 | 检查 | 找不到时 |
| --- | --- | --- |
| `CALL` / `CALLFORM` / `CALLF` / `CALLFORMF` / `JUMP` / `JUMPFORM` | 常量目标存在于全库标签表（`hasLabel`，与运行期同一判定） | error「指定的函数名"@X"不存在」 |
| `GOTO` / `GOTOFORM` | 常量 `$标签` 存在于**本函数内**（对齐 C# `GetLabelDollar`） | error「指定的标签名"$X"在当前函数内不存在」 |
| `TRYCALL` / `TRYJUMP` / `TRYCCALL` / `TRYCJUMP` / `TRYGOTO` / `TRYC*` 全族 | 不检查 | 运行期静默跳过（TRY 语义） |
| 目标含 `%…%` / `{…}` / `[[Rename]]` | 不检查 | 运行期求值，静态无从判定 |
| `CALLEVENT` / `BEGIN` / `FUNC` | 不检查 | C# 亦不做目标检查（`CALLEVENT` 缺目标静默） |

这与业界 linter 处理动态分派的做法一致：先建**全程序符号表**，只在目标**可证明静态**时下
结论，动态派发（字符串拼接、变量函数名）一律放过 —— 宁缺勿错。

实测：eraH 0 条；eraTW 6 条，全部是随游戏分发的第三方恋口上
（`ERATW用古明地恋口上《想起之途》2020 11 11`）里的 `CALL GAMEOVER` —— `@GAMEOVER` 在
eraTW 全库都没有定义，走到该分支必然运行期失败。appemuera 全量装载同样报出这 6 条
（`告警 6 条`），游戏本身照常装载并运行到标题。

顺带修复：标签名允许 `@`/`$` 之后的**前导空白**（`$ GOOUT` 的名字是 `GOOUT`）。C# 只
`ShiftNext()` 掉符号，名字由 `LexicalAnalyzer` 读（它会跳空白）；此前名字被切成空串，标签
根本没登记 —— eraTW 有 4 处这种写法（`NASIKUZUSI.ERB` 的 `$ GOOUT`、`COMF410 掃除.ERB`
的 `$ CANCEL_LOOP` 等），既漏报又会让运行期 `GOTO` 报 label not found。

### 还有哪些运行期错误被提前到装载期

收尾一共 6 步（可选第 7 步），其中 3 步是本次新增的「运行期错误静态化」，判据全部与 C#
装载器同源：

| 步骤 | 方法 | 检查 | 对齐的 C# |
| --- | --- | --- | --- |
| 4 | `resolveStaticTargets` | 常量跳转/调用目标存在（见上节） | `setJumpTo` |
| 4 | `checkCallStatementArgs` | `CALL` 的**实参个数/类型**：实参多于形参、字符串实参传给整型形参、REF 形参被省略或传非数组 | `CalledFunction.ConvertArg`（C# 也是在 `SetJumpTo` 里调的，即装载期） |
| 5 | `checkStructures` | 结构配对：`IF/ELSEIF/ELSE/ENDIF`、`REPEAT/REND`、`FOR/NEXT`、`WHILE/WEND`、`DO/LOOP`、`SELECTCASE/CASE/ENDSELECT`、`TRYC/CATCH/ENDCATCH`、`TRY*LIST/FUNC/ENDFUNC`、`PRINTDATA/DATA/DATALIST/ENDLIST/ENDDATA/STRDATA`、`NOSKIP/ENDNOSKIP`、循环外 `BREAK/CONTINUE`、受限块内定义 `$标签`、`SIF` 的次行、`RETURNF` 的位置与返回类型 | `ErbLoader.nestCheck` |
| 6 | `checkStaticValues` | 字面量除零（`X / 0`、`X % 0`） | 运行期 `CodeEE「0による除算が行なわれました」` |
| 7（可选） | `checkNotCalledFunctions` | 定义了但从未被调用的函数 | `checkScript` 的 `FunctionNotCalledWarning` |

两处**刻意不报**（宁缺勿错）：`CALL` 的实参少于形参（C# 对 `ARG`/`ARGS`/私有变量形参一律
补默认值，实际等价于「永远有默认值」）；`CSV/VariableSize.csv` 才声明长度的系统变量越界
（长度未知，`validateExpression` 已有的「变量 X 常量下标越界」只覆盖长度可证明的用户数组）。

第 7 步默认**关闭**，因为 C# 的 `関数が呼び出されなかった警告の扱い` 默认就是 `IGNORE`，
且只要脚本里出现过 `CALLFORM` 系（名字运行期求值）C# 就整体放弃该判定 —— 真实游戏几乎都
命中这条。分析器按游戏配置（`readFunctionNotCalledWarning`）决定是否开启，与 C# 一致。

eraTW 加装这三步后诊断数**仍是 6 条**（步骤 5/6 在 2.2M 行上分别只花 1.1s/1.2s）：eraTW 是
能正常玩通的成品，结构/实参/字面量除零都干净 —— 这同时是这三项检查「不误报」的实测证据。

### 编码告警的判定

「实际读出的编码 != 游戏声明的编码」（`DiagCode::encoding`）在**文件自带 BOM** 时不报：
BOM 无歧义，按它读出没有误读风险；只有无 BOM 的启发式探测结果与声明不符时才报。真实游戏
普遍如此（eraTW/eraH 已整体转 UTF-8+BOM，而 `emuera.config`/`CSV/_fixed.config` 的语言声明
仍是 JAPANESE/CHINESE_HANS）—— 不豁免 BOM 会变成每文件一条噪音。声明编码按**游戏根目录**
查找（`ERB` 的父目录），否则 `ERB/子目录/文件` 永远找不到根目录的配置文件；空文件不判定。

## LSP

启动 `build/src/tools/eralsp`，使用 stdin/stdout、`Content-Length` 分帧，协议功能对应 LSP 3.17。客户端应注册 `.erb`、`.erh` 文件，languageId 可设为 `erb`。

支持初始化与关闭、Full Sync 文档同步、推送诊断、补全、悬停、定义跳转和文档符号。补全与查询来自引擎登记表及 AST 标签；静态 GOTO 标签按函数作用域筛选。修改或关闭文件后重新分析工作区并为打开文档发布诊断。

初始化时递归索引 `rootUri`（或 `rootPath`）下的 ERH/ERB，使用引擎编码层读取；`rootUri` 是游戏根目录（其下直接有 `ERB` 子目录）时只索引该 `ERB` —— 与引擎装载范围一致，eraTW 这类仓库根下的 `パッチ`/`資料` 等目录（数百个补丁副本 .ERB）不参与分析；打开文档的缓冲区覆盖磁盘文本，关闭时恢复磁盘内容。收到 `workspace/didChangeWatchedFiles` 时重新索引，客户端需配置文件监视通知。未提供根目录时仅分析打开文档。当前仅支持一个根目录，未实现多根工作区。分析同步进行；取消通知不打断当前分析。没有增量同步、重命名、引用查询或动态跳转求值。大工作区每次全量分析的性能仍需后续优化。

## MCP

启动 `build/src/tools/eramcp`，stdio 每行一条 JSON-RPC 消息。协商支持 `2024-11-05`、`2025-03-26`、`2025-06-18`、`2025-11-25`。客户端应先发送 `initialize`（包含 `protocolVersion`、`capabilities`、`clientInfo`），再发送 `notifications/initialized`。

客户端配置示例（替换为实际绝对路径）：

```json
{
  "mcpServers": {
    "eraemu": {
      "command": "/your/install/prefix/bin/eramcp",
      "args": []
    }
  }
}
```

| 工具 | 用途 | 参数 |
| --- | --- | --- |
| `era_lookup` | 查询核心指令／表达式函数签名 | `name` |
| `era_search` | 搜索核心登记表 | `query`，可选 `limit`（1–1000） |
| `era_validate` | 单文档语义诊断与摘要；`path` 落在游戏 ERB 目录内时按**整局上下文**分析（按目录缓存） | `source` 或 `path`，可选 `fileName` |
| `era_symbols` | 单文档 AST 标签、逻辑行数及诊断；`path` 同上走整局上下文 | 同上 |
| `era_validate_workspace` | 完整 AST 跨文件语义分析 | `documents: [{fileName, source}, ...]`（1–256 个，名称唯一） |
| `era_stats` | 核心登记表条目数 | 无 |

工具结果提供 `structuredContent`，并在 `content` 中包含同样的 JSON 文本。脚本诊断是正常分析结果，读取失败、参数不符合 schema 等使用 `isError: true`。空字符串源码合法；`source` 和 `path` 必须二选一。文件读取使用引擎 `TextCodecUtil` 自动识别编码。

跨文件调用示例：

```json
{
  "name": "era_validate_workspace",
  "arguments": {
    "documents": [
      {"fileName": "main.erb", "source": "@MAIN\nPRINTS TEXT()\n"},
      {"fileName": "text.erb", "source": "@TEXT\n#FUNCTIONS\nRETURNF \"ok\"\n"}
    ]
  }
}
```

分析采用默认预处理设置，不自动装入游戏 CSV、`_Rename.csv`、配置或运行时变量，不求值依赖运行时的数组维度。未提交的依赖可能导致未定义符号诊断；没有诊断也不代表游戏能完整运行。扩展注册参与解析，但查询／搜索／计数目前只枚举核心 constexpr 登记表。

## 协议边界与测试

传输限制每条消息 16 MiB、LSP 头部 8 KiB。有效分帧内的错误 JSON 可恢复，分帧损坏则返回错误并退出。普通 stdout 被重定向到 stderr，协议使用独立输出描述符，避免引擎日志破坏消息流。

测试包括 JSON-RPC 格式／长度边界、真实管道交互与 stdout 隔离、LSP 生命周期／版本／作用域、MCP schema／编码／空源码，以及跨文件函数绑定、ERH 声明、宏和诊断归属。

参考：Qt 文档 `QJsonDocument`、`QJsonValue`、`QMap`、`QDirListing`；[LSP 3.17](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/)；[MCP 2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25)。
