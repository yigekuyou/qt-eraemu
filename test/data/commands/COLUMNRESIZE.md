# COLUMNRESIZE

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令
- **签名**：
  - `COLUMNRESIZE <列 ID>, <宽>, <高>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/read me(COLUMN_LIB).txt`「・COLUMNRESIZE(カラムID, width, height)」。ecd 套件与 zh 套件均未收录本命令。

## 语义

指定列的尺寸。数值以「全角 1 个字符 = 100%」为单位：1000 表示 10 个字符的宽度/高度。需先 `COLUMNCREATE` 创建该 ID 的列。

## 用法

### `COLUMNRESIZE <列 ID>, <宽>, <高>`
- `<列 ID>`：`COLUMNCREATE` 时指定的列编号。
- `<宽>`、`<高>`：数值，单位为全角 1 字 = 1000。
```erb
CALL COLUMNCREATE, 1
CALL COLUMNRESIZE, 1, 2000, 3000   ;宽 20 字、高 30 字
CALL COLUMNPRINTL, 1, "调整过大小的列。"
CALL COLUMNWAIT
```

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现该命令。它是 EmueraEE 发行版附带 ERB 库 COLUMN_LIB 的函数（作者 Enter，基于 `GDRAWTEXT`）。

EE 发行版的预期行为：按「全角 1 字 = 100%」的相对单位换算出像素尺寸，设置指定 ID 列的显示区域宽高。

## 备注

- source_index.md 标注「ABSENT from this repo's C# source（EE 发行版独有，未移植）」。
- ecd 与 zh 两套文档均未收录本命令。
