# COLUMNCLEAR

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令
- **签名**：
  - `COLUMNCLEAR`（参数形式未见文档记载；按同族命令推测可能为 `COLUMNCLEAR <列 ID>`）
- **文档来源**：无。`eraTW/README集/EmueraEE Readme/read me(COLUMN_LIB).txt` 未记载本命令（该 readme 只列了 COLUMNCREATE / COLUMNRESIZE / COLUMNMOVE / COLUMNCOLOR / COLUMNBGCOLOR / COLUMNDIRECTION / COLUMNPRINT / COLUMNPRINTL / COLUMNPRINTW / COLUMNWAIT），仓库内的 `ERB_EXCOM.khp` 关键字帮助中也无对应条目。ecd 套件与 zh 套件均未收录。

## 语义

文档未收录。按命令名与 COLUMN_LIB 同族命令的命名规律推断，其作用是清空指定列（カラム）中已缓冲/已显示的内容（列内版的 `CLEARLINE`/`CLEAR` 类操作）。**此语义仅为按名推断，未见任何文字佐证，使用前请在 EmueraEE 实机验证。**

## 用法

（无可引用的文档示例。按推断的用法如下，仅供参考。）

### `COLUMNCLEAR <列 ID>`（推测）
```erb
CALL COLUMNCREATE, 10
CALL COLUMNPRINTL, 10, "旧内容"
CALL COLUMNCLEAR, 10               ;推测：清空列 10 的内容
CALL COLUMNWAIT
```

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现该命令。它是 EmueraEE 发行版附带 ERB 库 COLUMN_LIB 的函数（作者 Enter，基于 `GDRAWTEXT`），但仓库内连说明文字都没有。

EE 发行版的预期行为（按名推断）：清空指定 ID 列的绘制缓冲与已显示内容。

## 备注

- source_index.md 标注「ABSENT from this repo's C# source（EE 发行版独有，未移植）」。
- 本命令是本批中唯一在两套中文文档、EE readme、khp 关键字帮助中均无任何记载的条目，仅出现在 `test/data/emuera_ee_cmds.txt`（由 C# 权威源码导出的 EE 命令清单）中。语义与签名均未经文档确认。
