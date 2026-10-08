# STRJOIN1

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（**伪名，实际不存在此命令**）
- **签名**：
  - 无（该名字不是任何真实命令）
- **文档来源**：所有文档（ecd Command.md、ERB_Commands.md、EmueraEE_readme.txt、EmueraEE_readme (English).txt、EmueraEE_changelog.txt、ERB_EXCOM.khp 关键字帮助、EM+EE 发行版 exe）中均**查无此名**。

## 语义

不存在名为 `STRJOIN1` 的命令/函数。它是命令清单提取时的伪名：EM 系 readme（`eraTW/README集/EmueraEE Readme/私家改造版Emuera_readme.txt`）中存在「STRJOINの第1引数が……」之类的描述文本（约 142 行），「STRJOIN」+「第1」字样被误提取成 `STRJOIN1`。清单生成脚本（`test/export_command_tables.py` 的 `EE_EXTENSIONS` 手工清单）将其误收入。

### 伪名验证

按工作约定核查了「前缀 F + 原名」伪名可能性：`STRJOIN1` 无 F 前缀，不是该模式；真正的 F 前缀伪名是 `FSTRJOIN`（对应真实命令 `STRJOIN`，归 ee02 批）。`STRJOIN1` 经以下途径逐一验证均不存在：

1. `source_index.md:258` 标 **NOT FOUND**，`:479` 标 **ABSENT**；
2. EE 三份 readme 及 changelog 无此名（仅 `STRJOIN`）；
3. ERB_EXCOM.khp 关键字帮助无此名（仅 `STRJOIN` 相关）；
4. EM+EE 发行版 exe（`eraTW/Emuera1824+v18+EMv17+EEv41.exe`）按 ASCII 与 UTF-16 两种编码检索均无此标识符；
5. EM 文档站 evilmask.gitlab.io/emuera.em.doc 只有 `SPLIT, STRJOIN`，无 `STRJOIN1`。

### 真实对应物

字面上的真实命令是 **`STRJOIN`**（字符串数组连接函数）：本仓库已有实现，注册于 `Runtime/Script/Statements/Function/Creator.cs:137`（`["STRJOIN"] = new JoinMethod()`），实现于 `Runtime/Script/Statements/Function/Creator.Method.cs:4944`（`JoinMethod`），其内部恰好就有对「第 1/第 2 引数（连接符、起始位置、元素数）」的合法性检查（`Runtime/Script/Statements/Function/Creator.Method.cs:4993-4996`），与上述 readme 描述文本吻合。该命令的完整文档见 `test/data/functions/STRJOIN.md`（fn13 批）。

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现该命令——因为它不存在。EE 发行版中同样不存在，故也没有「EE 发行版的预期行为」可述；相关功能即 `STRJOIN` 本身。

## 备注

- 结论：本文件仅为占位说明，`STRJOIN1` 应从 EE 扩展命令清单中剔除。
- 生成清单的 `test/export_command_tables.py` 中 `EE_EXTENSIONS` 列表（"字符串/调用扩展"分组末尾的 `"STRJOIN1"`）是错误来源。
