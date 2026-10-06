# COLUMNWAIT

- **类别**：EE 扩展命令
- **签名**：
  - `COLUMNWAIT`
- **文档来源**：`eraTW/README集/EmueraEE Readme/read me(COLUMN_LIB).txt`「・COLUMNWAIT」。ecd 套件与 zh 套件均未收录本命令。

## 语义

触发列的画面重绘。COLUMN_LIB 的输出是惰性重绘的：`COLUMNPRINT`/`COLUMNPRINTL` 只把文字写入列缓冲，只有当执行了 `COLUMNPRINTW` 或 `COLUMNWAIT` 时才重绘画面。因此用 `COLUMNPRINT`/`COLUMNPRINTL` 批量输出后，最后调用一次 `COLUMNWAIT` 即可。

## 用法

### `COLUMNWAIT`
无参数。
```erb
CALL COLUMNCREATE, 9
CALL COLUMNRESIZE, 9, 2000, 2000
CALL COLUMNPRINTL, 9, "第 1 行"
CALL COLUMNPRINTL, 9, "第 2 行"
CALL COLUMNPRINTL, 9, "第 3 行"
CALL COLUMNWAIT                   ;三行一次重绘
```

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现该命令。它是 EmueraEE 发行版附带 ERB 库 COLUMN_LIB 的函数（作者 Enter，基于 `GDRAWTEXT`）。

EE 发行版的预期行为：刷新（重绘）包含各列的画面，使此前缓冲的列输出真正显示出来。

## 备注

- source_index.md 标注「ABSENT from this repo's C# source（EE 发行版独有，未移植）」。
- ecd 与 zh 两套文档均未收录本命令。
- 与主画面的 `WAIT`（等待用户输入）无关，名字虽含 WAIT 但语义是「等待重绘时机/执行重绘」。
