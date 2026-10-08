# GETTEXTSIZE

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令
- **签名**：
  - （EE 发行版清单中收录的名字；无独立签名，见备注）
- **文档来源**：`EmueraEE_readme.txt` 中出现 `・GETTEXTSIZE` 条目（其描述实为 GETTEXTBOX 的语义，疑为笔误）；`ecd/Command.md` 未收录；EM+EE 在线文档无此页；zh 套件未收录。

## 语义

**本仓库（emuera.em/Emuera）未实现该命令，且极可能是一个不存在的名字（文档笔误）。** EE 日文 readme 中的 `GETTEXTSIZE` 条目写着「実行時点でテキストボックスに入力されている内容を取得する」（取得执行时文本框内已输入的内容），这与 `GETTEXTBOX` 的语义完全一致；EE changelog v21 记载的也是「GETTEXTBOX,SETTEXTBOX追加」，EM+EE 在线文档同样只有 GETTEXTBOX 页而无 GETTEXTSIZE 页。因此 readme 中的 GETTEXTSIZE 应视为 GETTEXTBOX 的笔误。

EE 发行版的预期行为（若按字面将其视为独立命令）：获取执行时文本框内已输入的内容，返回/代入字符串（等价于 GETTEXTBOX）。

注意不要与已有的 `GGETTEXTSIZE`（预测 GDRAWTEXT 绘制尺寸，返回宽度并设 RESULT:1 为高度）混淆，后者才是真实存在的图形测量命令。

## 用法

### （无可靠用法）
```erb
;该名字无权威签名。若确需取得文本框内容，应使用 GETTEXTBOX：
INPUT
PRINTFORMW 输入框内容 %GETTEXTBOX%
;若需测量绘制文本的尺寸，应使用 GGETTEXTSIZE：
GGETTEXTSIZE "USA", "Arial", 150
PRINTFORML Width:{RESULT:0} Height:{RESULT:1}
```

## 源码实现（emuera.em/Emuera）

- 注册：无（`source_index.md` 仅以 raw-grep 命中 `#region EE_GGETTEXTSIZE`，属于 GGETTEXTSIZE 的模糊匹配）
- 实现：无

```text
本仓库（emuera.em/Emuera）未实现该命令。
EE 发行版的预期行为：按 readme 条目描述为「取得执行时文本框内已输入的内容」，
与 GETTEXTBOX 相同；该条目极可能是 GETTEXTBOX 的笔误，而非独立命令。
```

## 备注

- 判定为文档笔误的证据链：EE changelog v21 只记载 GETTEXTBOX/SETTEXTBOX；readme 中 GETTEXTSIZE 条目的描述文字与 GETTEXTBOX 语义一字不差；EM+EE 在线文档无 GETTEXTSIZE 命令页。
- 本仓库 C# 源码中 `grep GETTEXTSIZE`（排除 GGETTEXTSIZE）无任何命中。
