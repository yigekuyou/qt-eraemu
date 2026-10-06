# COLUMNDIRECTION

- **类别**：EE 扩展命令
- **签名**：
  - `COLUMNDIRECTION <列 ID>, <0 或 1>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/read me(COLUMN_LIB).txt`「・COLUMNDIRECTION(カラムID, 0or1)」。ecd 套件与 zh 套件均未收录本命令。

## 语义

指定列内文字的流动（滚动）方向。

- `0`（默认）：与 Emuera 主画面一样，日志**自下而上**流动（新内容出现在下方，旧行上移）。
- `1`：与普通文档一样，日志**自上而下**流动（新内容追加在下方，旧行不动）。

## 用法

### `COLUMNDIRECTION <列 ID>, <0 或 1>`
- `<列 ID>`：`COLUMNCREATE` 时指定的列编号。
- `<0 或 1>`：0 = 自下而上（Emuera 式），1 = 自上而下（文档式）。
```erb
CALL COLUMNCREATE, 5
CALL COLUMNDIRECTION, 5, 1        ;像文档一样从上往下追加
CALL COLUMNPRINTL, 5, "第 1 行"
CALL COLUMNPRINTL, 5, "第 2 行（显示在第 1 行下面）"
CALL COLUMNWAIT
```

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现该命令。它是 EmueraEE 发行版附带 ERB 库 COLUMN_LIB 的函数（作者 Enter，基于 `GDRAWTEXT`）。

EE 发行版的预期行为：设置指定 ID 列的日志流动方向标志（0 = 自下而上，1 = 自上而下）。

## 备注

- source_index.md 标注「ABSENT from this repo's C# source（EE 发行版独有，未移植）」。
- ecd 与 zh 两套文档均未收录本命令。
