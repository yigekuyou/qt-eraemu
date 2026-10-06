# OCLEARLINE

- **类别**：EE 扩展命令
- **签名**：
  - `OCLEARLINE <行数>`（按仓库内测试桩推断的签名）
- **文档来源**：无权威文档收录——`ecd/Command.md` 未收录；zh 套件未收录；`EmueraEE_readme.txt` / `EmueraEE_changelog.txt` 未提及；EM+EE 在线文档 Reference 列表中无此页；eraTW `ERB_EXCOM.khp` 中亦无此条目。本仓库自带的覆盖测试（`test/data/doc_smoke.tsv`，组35 清单）将其按「删除已显示行（EE）」造桩。

## 语义

**本仓库（emuera.em/Emuera）未实现该命令；无权威文档，语义只能按名字与仓库内测试桩推测。**

从命名看，它是 `CLEARLINE`（删除已显示的行）的变体，前缀 `O` 可能来自 EM 系扩展的显示/对象（object/overlay）语境。EE changelog 中曾出现「CLEARLINE 与 div（EM 的分栏显示功能）併用时的不具合修正」等条目，说明 EM+EE 系对 CLEARLINE 在分栏/覆盖显示下有扩展行为，`OCLEARLINE` 很可能是与之配套的「删除已显示行」的 EM/EE 变体命令。本仓库测试桩将其按「删除已显示行」造桩并断言执行不报错。

EE 发行版的预期行为：删除已显示的若干行（类似 CLEARLINE），但针对的对象/区域与普通 CLEARLINE 不同（具体无资料确证）。

## 用法

### `OCLEARLINE <行数>`（推断）
- `<行数>`：数值表达式，要删除的已显示行数。
```erb
;仓库内覆盖测试（test/example/ERB/16_DISPLAY.ERB）的用法：
OCLEARLINE 1
;真实 EM+EE 发行版中的实际行为无资料确证
```

## 源码实现（emuera.em/Emuera）

- 注册：无（`source_index.md` 标注 ABSENT / NOT FOUND）
- 实现：无

```text
本仓库（emuera.em/Emuera）未实现该命令。
EE 发行版的预期行为：推测为「删除已显示行」的 EM/EE 变体（类似 CLEARLINE，
可能作用于 EM 分栏/覆盖显示下的已显示内容）；无文档与源码可考，具体语义不明。
```

## 备注

- 在 ecd、zh、EE readme/changelog、EM+EE 在线文档 sitemap、eraTW khp 中均检索不到 OCLEARLINE；在线检索（Web）亦无结果。
- 本仓库测试桩（`test/example/ERB/16_DISPLAY.ERB:31-33`）只验证「执行不报错」，`test/data/doc_smoke.tsv` 里的复核原因注为「删除已显示行（EE）」，均为本仓库作者的同名推测，非权威语义。
- 若要确定其真实语义，需取得 EM+EE 原始发行版的 Emuera.exe/源码或其 wiki 的 EM 扩展命令页查证。
