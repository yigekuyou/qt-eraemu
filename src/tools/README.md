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
3. 调用 `EraParseTable::finalizeParse()`，对已构造 AST 完成跨文件声明、表达式函数绑定、参数及类型检查。
4. 输出来自引擎的结构化诊断与 AST 标签。每次分析建立新快照，避免删除文件后残留符号。

引擎修改保留了诊断的文件、物理行、列和跨度，并让不同位置的同类错误分别报告。协议返回的位置统一为 **0 起始、UTF-16 代码单元**。

## LSP

启动 `build/src/tools/eralsp`，使用 stdin/stdout、`Content-Length` 分帧，协议功能对应 LSP 3.17。客户端应注册 `.erb`、`.erh` 文件，languageId 可设为 `erb`。

支持初始化与关闭、Full Sync 文档同步、推送诊断、补全、悬停、定义跳转和文档符号。补全与查询来自引擎登记表及 AST 标签；静态 GOTO 标签按函数作用域筛选。修改或关闭文件后重新分析工作区并为打开文档发布诊断。

初始化时递归索引 `rootUri`（或 `rootPath`）下的 ERH/ERB，使用引擎编码层读取；打开文档的缓冲区覆盖磁盘文本，关闭时恢复磁盘内容。收到 `workspace/didChangeWatchedFiles` 时重新索引，客户端需配置文件监视通知。未提供根目录时仅分析打开文档。当前仅支持一个根目录，未实现多根工作区。分析同步进行；取消通知不打断当前分析。没有增量同步、重命名、引用查询或动态跳转求值。大工作区每次全量分析的性能仍需后续优化。

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
| `era_validate` | 单文档语义诊断与摘要 | `source` 或 `path`，可选 `fileName` |
| `era_symbols` | 单文档 AST 标签、逻辑行数及诊断 | 同上 |
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
