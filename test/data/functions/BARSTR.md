# BARSTR

- **类别**：式中函数（另有同名命令形态）
- **签名**：str BARSTR(int value, int max, int length)
- **文档来源**：`ecd/Command.md`「### BARSTR `<变量>`, `<最大值>`, `<长度>`」；`ecd/Expression.md` 签名列表 `str BARSTR(int value, int max, int length)`；zh 套件未收录

## 语义

绘制一个 BAR（条、槽）并返回其字符串形式。根据 `value` 相对 `max` 的比例，用两种 BAR 字符（配置项 `BAR文字1`/`BAR文字2`，默认 `*` 和 `.`）填充出总长 `length` 的条，两侧加上 `[`、`]`。与 `BAR` 指令类似，但 `BAR` 指令把结果打印到画面，本函数把字符串作为返回值交给调用者。

同名命令形态 `BARSTR <变量>, <最大值>, <长度>` 把结果字符串赋值给 `RESULTS:0`（ecd/Command.md 有独立小节）。本文档以式中函数形态为主。

错误行为：
- `max <= 0`：抛出 CodeEE（最大值必须为正）。
- `length <= 0`：抛出 CodeEE（长度必须为正）。
- `length >= 100`：抛出 CodeEE（长度过长，为防失控而限制）。

填充数量 `count = value * length / max`（unchecked 整数运算，可能溢出），并截断到 `0 ～ length` 范围（负值按 0 处理）。即 `value` 为负时整个条全空，`value >= max` 时整个条全满。

## 用法

### str BARSTR(int value, int max, int length)
- `value`：当前值。
- `max`：最大值（满条时的值），必须为正。
- `length`：条的总长度（字符数），1～99。
- 返回值：形如 `[****.....]` 的字符串。
```erb
LOCALS = %BARSTR(50, 100, 10)%   ; LOCALS = "[*****.....]"
PRINTFORML 体力 {BASE:0}/{MAXBASE:0} {BARSTR(BASE:0, MAXBASE:0, 20)}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:50`（`["BARSTR"] = new BarStringMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2661`（`BarStringMethod`），转调 `ExpressionMediator.CreateBar`
- 核心算法：`Runtime/Script/Statements/ExpressionMediator.cs:118`（`CreateBar`）

```text
BarStringMethod:
构造：返回类型 = string；参数 = [long, long, long]；CanRestructure = true。
GetStrValue(exm, args):
    var    ← args[0].GetIntValue(exm)
    max    ← args[1].GetIntValue(exm)
    length ← args[2].GetIntValue(exm)
    返回 ExpressionMediator.CreateBar(var, max, length)

CreateBar(var, max, length):
    若 max <= 0:   抛出 CodeEE（BAR 的最大值必须为正数）
    若 length <= 0: 抛出 CodeEE（BAR 的长度必须为正数）
    若 length >= 100: 抛出 CodeEE（BAR 过长）        ; 暴走防止
    count ← (int)(var * length / max)               ; unchecked，可能溢出
    若 count < 0:    count ← 0
    若 count > length: count ← length
    返回 "[" + BarChar1 × count + BarChar2 × (length - count) + "]"
    ; BarChar1/BarChar2 来自配置（默认 '*' 与 '.'）
```

## 备注

- ecd/Command.md 的小节描述的是命令形态（结果赋给 `RESULTS:0`）；Expression.md 收录函数形态 `str BARSTR(int value, int max, int length)`，两者共用同一算法。
- 文档未说明 `length` 上限 100、`max` 必须为正等错误行为，这些来自源码。
- `value*length` 在 unchecked 上下文中计算，极端大值会溢出环绕，文档未提及。
