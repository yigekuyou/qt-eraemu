# COLUMNBGCOLOR

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令
- **签名**：
  - `COLUMNBGCOLOR <列 ID>, <背景色>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/read me(COLUMN_LIB).txt`「・COLUMNBGCOLOR(カラムID, 背景色)」。ecd 套件与 zh 套件均未收录本命令。

## 语义

指定列的背景色。参数规则与 `COLUMNCOLOR` 相同：**第 2 参数是字符串型**，传 `COLOR_FROMNAME` 兼容的颜色名。

## 用法

### `COLUMNBGCOLOR <列 ID>, <背景色>`
- `<列 ID>`：`COLUMNCREATE` 时指定的列编号。
- `<背景色>`：字符串，`COLOR_FROMNAME` 兼容的颜色名。
```erb
CALL COLUMNCREATE, 4
CALL COLUMNCOLOR, 4, "Black"
CALL COLUMNBGCOLOR, 4, "White"
CALL COLUMNPRINTL, 4, "白底黑字。"
CALL COLUMNWAIT
```

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现该命令。它是 EmueraEE 发行版附带 ERB 库 COLUMN_LIB 的函数（作者 Enter，基于 `GDRAWTEXT`）。

EE 发行版的预期行为：把字符串色名转换后作为指定 ID 列的背景填充色。

## 备注

- source_index.md 标注「ABSENT from this repo's C# source（EE 发行版独有，未移植）」。
- ecd 与 zh 两套文档均未收录本命令。
