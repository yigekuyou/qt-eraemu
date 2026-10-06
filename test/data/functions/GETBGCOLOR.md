# GETBGCOLOR

- **类别**：式中函数
- **签名**：int GETBGCOLOR()
- **文档来源**：`ecd/Command.md`「### GETBGCOLOR」（与 GETCOLOR 类似的一组指令章节）；`ecd/Expression.md` 内置函数一览（`int GETBGCOLOR()`）；zh 套件未收录

## 语义

返回当前使用的背景颜色，赋值给返回值（旧指令形态时为 `RESULT:0`）。返回值以 16 进制表示为 `0xRRGGBB` 格式（不含 alpha）：例如背景色为橙色 RGB(255,128,0) 时返回 `16744448`（`0xFF8000`）。

返回的是控制台当前实际使用的背景色；配置指定的默认背景色由 `GETDEFBGCOLOR` 返回。与本函数同组的还有 `GETCOLOR`（文字色）、`GETDEFCOLOR`、`GETFOCUSCOLOR`（被选中文字色）。

## 用法

### int GETBGCOLOR()
- 无参数。
- 返回值：当前背景色，`0xRRGGBB` 整数。
```erb
c = GETBGCOLOR()
PRINTFORML 背景色 = 0x{c,6:X6}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:46`（`["GETBGCOLOR"] = new GetBGColorMethod(false)`；同类的 `GETDEFBGCOLOR` 为 `Runtime/Script/Statements/Function/Creator.cs:46` 的 `true` 版本）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2604`（`GetBGColorMethod`）

```text
GetBGColorMethod(isDef = false):
构造：返回类型 = long；参数 = []；
    CanRestructure = isDef（GETDEFBGCOLOR 为常量可折叠，GETBGCOLOR 不可）；
    defaultColor = isDef。

GetIntValue(exm, args):
    color = defaultColor ? Config.BackColor      ; GETDEFBGCOLOR：配置的默认背景色
                         : GlobalStatic.Console.bgColor   ; GETBGCOLOR：控制台当前背景色
    返回 color.ToArgb() & 0xFFFFFF               ; 取低 24 位，丢弃 alpha → 0xRRGGBB
```

## 备注

- ecd/Command.md 按旧版 Emuera 的「指令」形式记载（结果赋给 RESULT:0）；本仓库中它注册为纯式中函数（FunctionMethod），在表达式中直接取返回值。旧脚本若使用 `GETBGCOLOR` 指令形态需要改写为 `RESULT = GETBGCOLOR()`。
- `GETCOLOR` 文档小节（ecd/Command.md）给出的 `0xRRGGBB` 格式说明同样适用于本函数，实现一致（`ToArgb() & 0xFFFFFF`）。
- 与 `GETDEFBGCOLOR` 共用实现类，仅构造参数不同；这也解释了 Expression.md 中两者签名相同。
- zh 套件未收录本函数。
