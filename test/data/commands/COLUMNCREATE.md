# COLUMNCREATE

- **类别**：EE 扩展命令
- **签名**：
  - `COLUMNCREATE <列 ID>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/read me(COLUMN_LIB).txt`「・COLUMNCREATE(カラムID)」。ecd 套件与 zh 套件均未收录本命令。

## 语义

按指定的 ID 创建一个「列（カラム）」。列是 EmueraEE 作者 Enter 提供的试作库 COLUMN_LIB 的功能，内部用 EmueraEE 的 `GDRAWTEXT` 在画面上生成一个独立的可滚动文本区域（列）。

- 若该 ID 的列已经生成过，返回 0（不重复创建）；创建成功返回非 0。
- 创建后可用 `COLUMNRESIZE`/`COLUMNMOVE`/`COLUMNCOLOR`/`COLUMNBGCOLOR`/`COLUMNDIRECTION` 设置尺寸、位置与颜色，用 `COLUMNPRINT` 系向列内输出文字。
- COLUMN_LIB 中的全部函数都是 ERB 普通函数（用 `CALL` 调用），不是引擎内建命令。

## 用法

### `COLUMNCREATE <列 ID>`
- `<列 ID>`：数值。标识一个列，后续所有 COLUMN 系命令都用它引用该列。
```erb
CALL COLUMNCREATE, 0
SIF RESULT == 0
    PRINTL 列 0 已存在。
CALL COLUMNRESIZE, 0, 2000, 3000   ;宽 20 字、高 30 字
CALL COLUMNMOVE, 0, 0, 0
CALL COLUMNCOLOR, 0, "White"
CALL COLUMNPRINTL, 0, "你好，列世界。"
CALL COLUMNWAIT
```

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现该命令。它是 EmueraEE 发行版附带的 ERB 库（COLUMN_LIB，作者 Enter）中的普通 ERB 函数，仓库内只有其说明文档（`eraTW/README集/EmueraEE Readme/read me(COLUMN_LIB).txt`），没有 C# 实现。

EE 发行版的预期行为：以指定 ID 创建列对象（内部基于 `GDRAWTEXT` 生成的文本位图区）；ID 已被占用时不重建并返回 0，新创建时返回成功标志（非 0）。

## 备注

- source_index.md 标注「ABSENT from this repo's C# source（EE 发行版独有，未移植）」。
- 注意与 EM 区的 `DT_COLUMN_OPTIONS`（`Runtime/Script/Statements/BuiltInFunctionCode.cs` `#region EM`）无关，那只是下拉列框选项的枚举，不是本命令。
