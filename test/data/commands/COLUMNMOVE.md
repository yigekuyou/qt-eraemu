# COLUMNMOVE

- **类别**：EE 扩展命令
- **签名**：
  - `COLUMNMOVE <列 ID>, <X 坐标>, <Y 坐标>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/read me(COLUMN_LIB).txt`「・COLUMNMOVE(カラムID, X座標, Y座標)」。ecd 套件与 zh 套件均未收录本命令。

## 语义

指定列的显示位置。数值以「全角 1 个字符 = 100%」为单位（与 `COLUMNRESIZE` 相同）。

## 用法

### `COLUMNMOVE <列 ID>, <X 坐标>, <Y 坐标>`
- `<列 ID>`：`COLUMNCREATE` 时指定的列编号。
- `<X 坐标>`、`<Y 坐标>`：数值，单位为全角 1 字 = 1000。
```erb
CALL COLUMNCREATE, 2
CALL COLUMNRESIZE, 2, 1500, 1000
CALL COLUMNMOVE, 2, 500, 2000      ;把列移到 (5 字, 20 字) 处
CALL COLUMNPRINTL, 2, "被移动过的列。"
CALL COLUMNWAIT
```

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现该命令。它是 EmueraEE 发行版附带 ERB 库 COLUMN_LIB 的函数（作者 Enter，基于 `GDRAWTEXT`）。

EE 发行版的预期行为：把指定 ID 列的显示区域移动到指定坐标处，坐标按「全角 1 字 = 100%」换算。

## 备注

- source_index.md 标注「ABSENT from this repo's C# source（EE 发行版独有，未移植）」。
- ecd 与 zh 两套文档均未收录本命令。
