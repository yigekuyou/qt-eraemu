# LCSVISASSI

- **类别**：EE 扩展命令
- **签名**：
  - （无权威签名；仅作为 EM+EE 发行版关键字收录）
- **文档来源**：无任何文档收录——`ecd/Command.md` 未收录；zh 套件未收录；`EmueraEE_readme.txt` / `EmueraEE_changelog.txt` 未提及；EM+EE 在线文档（evilmask.gitlab.io/emuera.em.doc）Reference 列表中无此页；eraTW 关键字帮助 `ERB_EXCOM.khp` 中亦无此条目。

## 语义

**本仓库（emuera.em/Emuera）未实现该命令，且未找到任何记载其语义的文档。**

该名字仅出现在本仓库据以建表的 EM+EE 发行版关键字清单（`test/data/emuera_ee_cmds.txt`）中。从命名推测，它可能是某 EM+EE 系变体的私有扩展：`LC` 常见于 `PRINTLC`（左对齐 PRINTC）系前缀，`ISASSI` 是 EraBasic 的「助手」角色变量，故该命令可能用于显示/输出与助手（ASSI）相关的定宽文本，但这只是由名字作出的推测，无法确证。

EE 发行版的预期行为：无资料，无法简述；若确需此功能应回到该发行版（eraTW 所附 EM+EE）的实际 ERB 代码中查证其用法。

## 用法

### （无权威用法）
```erb
;无可靠示例。请勿在新代码中使用该名字。
```

## 源码实现（emuera.em/Emuera）

- 注册：无（`source_index.md` 标注 ABSENT / NOT FOUND）
- 实现：无

```text
本仓库（emuera.em/Emuera）未实现该命令。
EE 发行版的预期行为：无任何文档与源码可考，语义不明。
全仓库（含 eraTW、test 提取材料）grep 均无该名字的实际使用或定义。
```

## 备注

- 在全部提取材料（ecd、zh、EE readme/changelog、EM+EE 在线文档 sitemap、eraTW khp）中均检索不到 LCSVISASSI；在线检索（Web）亦无结果。
- 本仓库的测试桩 `test/example/ERB/10_COVERAGE.ERB:173` 仅以裸名字出现，未给出语义。
- 属「清单收录但完全无据可考」的条目，文档按「语义不明」如实记录。
