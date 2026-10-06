# CURRENTREDRAW

- **类别**：式中函数
- **签名**：int CURRENTREDRAW()
- **文档来源**：`ecd/Command.md`「### CURRENTREDRAW」小节；`ecd/Expression.md`「内置表达式内函数一览」`int CURRENTREDRAW()`；zh 套件未收录

## 语义

把当前画面的绘制方式（`REDRAW` 指令设定的状态）以数值返回：画面自动绘制被暂停（`REDRAW 0`）时返回 `0`，处于自动绘制状态时返回 `1`。默认（未使用过 `REDRAW` 时）返回 `1`。无参数、无副作用；读取的是控制台当前的 redraw 状态，因此不可常量折叠（`CanRestructure = false`）。

## 用法

### int CURRENTREDRAW()
- 无参数（`()` 不能省略，以与变量区分）。
- 返回值：`0` = 画面自动绘制已暂停，`1` = 正在自动绘制。
```erb
REDRAW 0
PRINTV CURRENTREDRAW()       ; 输出 0
REDRAW 1
PRINTV CURRENTREDRAW()       ; 输出 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:52`（`["CURRENTREDRAW"] = new CurrentRedrawMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2697`（`CurrentRedrawMethod`）

```text
CurrentRedrawMethod:
构造：返回类型 = long；参数表 = []（无参）；CanRestructure = false。
GetIntValue(exm, args):
    若 exm.Console.Redraw == GameView.ConsoleRedraw.None → 返回 0L
    否则                                            → 返回 1L
```

## 备注

- ecd/Command.md 称结果「返回到 RESULT:0 中」，本函数实际为式中函数，数值直接作为表达式的值返回；Expression.md 的 `int CURRENTREDRAW()` 与源码一致。
- 源码只区分 `ConsoleRedraw.None`（0）与其余一切状态（1）；`REDRAW 2/3` 的「强制更新一次」语义在本函数的返回值上与 1 无区别。
