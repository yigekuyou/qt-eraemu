# TINPUTAWAIT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（**伪名，实际不存在此命令**）
- **签名**：
  - 无（该名字不是任何真实命令）
- **文档来源**：所有文档（ecd Command.md、EmueraEE_readme.txt 及英文版、EmueraEE_changelog.txt、ERB_EXCOM.khp 关键字帮助、EM 文档站）中均**查无此名**。

## 语义

不存在名为 `TINPUTAWAIT` 的命令/函数。它是命令清单提取时的伪名：EM 系 readme（`eraTW/README集/EmueraEE Readme/私家改造版Emuera_readme.txt` 约 120 行）有一句更新日志「テスト版で行った残り時間非表示のTINPUTとAWAITの挙動変更を取り込み」（吸收了测试版中对剩余时间隐藏的 `TINPUT` 与 `AWAIT` 的行为修改），「TINPUT」与「AWAIT」相邻出现被误连写成 `TINPUTAWAIT`。清单生成脚本（`test/export_command_tables.py` 的 `EE_EXTENSIONS` 手工清单，"输入扩展"分组）将其误收入。

### 伪名验证

按工作约定核查了「前缀 F + 原名」伪名可能性：`TINPUTAWAIT` 无 F 前缀，不是该模式。经以下途径逐一验证均不存在：

1. `source_index.md:259` 标 **NOT FOUND**，`:480` 标 **ABSENT**；
2. EE readme/changelog 无此名（changelog:36 只提到「TINPUT系でマウスクリックオプションを付けたとき…」）；
3. ERB_EXCOM.khp 关键字帮助中 TINPUT 系只有 `TINPUT`、`TINPUTS`；
4. EM+EE 发行版 exe（`eraTW/Emuera1824+v18+EMv17+EEv41.exe`）按 ASCII 与 UTF-16 两种编码检索均无 `TINPUTAWAIT` 标识符；
5. EM 文档站 evilmask.gitlab.io/emuera.em.doc 输入系只有 `TINPUT(S)`，`AWAIT` 是独立条目。

### 真实对应物

- **`TINPUT` / `TINPUTS`**：带计时器的输入命令（本仓库有实现，见 ecd Command.md 输入系小节）。
- **`AWAIT`**：暂停 ERB 执行、进行 Windows 消息处理的命令（ecd Command.md「AWAIT 相关」小节有专述；本仓库实现于 `Runtime/Script/Statements/Instraction.Child.cs:2662` `AWAIT_Instruction`）。

两者是独立的命令，EE 发行版只是把测试版中对它们的计时/行为修改合并进了正式版，并没有新增组合命令。

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现该命令——因为它不存在。EE 发行版中同样不存在，故也没有「EE 发行版的预期行为」可述。

## 备注

- 结论：本文件仅为占位说明，`TINPUTAWAIT` 应从 EE 扩展命令清单中剔除。
- 生成清单的 `test/export_command_tables.py` 中 `EE_EXTENSIONS` 列表（"输入扩展"分组末尾的 `"TINPUTAWAIT"`）是错误来源。
