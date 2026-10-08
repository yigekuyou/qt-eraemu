# COLUMNPRINTW

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令
- **签名**：
  - `COLUMNPRINTW <列 ID>, <字符串>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/read me(COLUMN_LIB).txt`「・COLUMNPRINTW(カラムID, 文字列)」「・COLUMNWAIT」。ecd 套件与 zh 套件均未收录本命令。

## 语义

向指定列内输出文字并换行，然后**触发列的画面重绘**，相当于列内版的 `PRINTW`。列功能的重绘是惰性的：只有执行了 `COLUMNPRINTW` 或 `COLUMNWAIT` 时才会重绘画面。

## 用法

### `COLUMNPRINTW <列 ID>, <字符串>`
- `<列 ID>`：`COLUMNCREATE` 时指定的列编号。
- `<字符串>`：要写入列内的一行文字。
```erb
CALL COLUMNCREATE, 8
CALL COLUMNPRINT, 8, "这两行"
CALL COLUMNPRINTL, 8, "先缓冲着。"
CALL COLUMNPRINTW, 8, "到这行才一起显示出来。"
```

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现该命令。它是 EmueraEE 发行版附带 ERB 库 COLUMN_LIB 的函数（作者 Enter，基于 `GDRAWTEXT`）。

EE 发行版的预期行为：输出字符串并换行后重绘指定列所在的画面（等价于 `COLUMNPRINTL` + `COLUMNWAIT`）。

## 备注

- source_index.md 标注「ABSENT from this repo's C# source（EE 发行版独有，未移植）」。
- ecd 与 zh 两套文档均未收录本命令。
