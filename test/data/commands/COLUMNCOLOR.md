# COLUMNCOLOR

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令
- **签名**：
  - `COLUMNCOLOR <列 ID>, <文字色>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/read me(COLUMN_LIB).txt`「・COLUMNCOLOR(カラムID, 文字色)」。ecd 套件与 zh 套件均未收录本命令。

## 语义

指定列中显示文字的颜色。**注意第 2 参数是字符串型**，要传 `COLOR_FROMNAME` 能识别的颜色名（如 `"White"`、`"Red"` 等预定义色名）。与命令 `SETCOLORBYNAME` / 式中函数 `COLOR_FROMNAME` 同一体系。

## 用法

### `COLUMNCOLOR <列 ID>, <文字色>`
- `<列 ID>`：`COLUMNCREATE` 时指定的列编号。
- `<文字色>`：字符串，`COLOR_FROMNAME` 兼容的颜色名。
```erb
CALL COLUMNCREATE, 3
CALL COLUMNCOLOR, 3, "Yellow"
CALL COLUMNPRINTL, 3, "黄色文字。"
CALL COLUMNWAIT
```

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现该命令。它是 EmueraEE 发行版附带 ERB 库 COLUMN_LIB 的函数（作者 Enter，基于 `GDRAWTEXT`）。

EE 发行版的预期行为：把字符串色名经 `COLOR_FROMNAME` 转换后，作为指定 ID 列的文字绘制色。

## 备注

- source_index.md 标注「ABSENT from this repo's C# source（EE 发行版独有，未移植）」。
- ecd 与 zh 两套文档均未收录本命令。
