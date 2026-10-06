# POWER

- **类别**：式中函数（另有同名命令形态）
- **签名**：int POWER(int x, int y)
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；`ecd/Command.md`「### POWER `<变量>`, `<数值表达式>`, `<数值表达式>`」（命令形态）

## 语义

返回 x 的 y 次幂（x^y），结果为 64 位整数。内部用浮点运算（`Math.Pow`）计算后转回整数。

错误行为（命令文档「运算结果溢出时会出错」在源码中细化为三种）：
- 计算结果为非数值（NaN）时抛出 CodeEE；
- 结果为无穷大时抛出 CodeEE；
- 结果超出 64 位符号整数范围时抛出 CodeEE。

同名命令形态 `POWER <变量>, x, y` 把 x 的 y 次幂直接赋值给指定变量（如 `POWER A, X, Y` 把 X 的 Y 次幂赋给 A），本仓库另有命令式注册（Process.ScriptProc.cs:341，SP_POWER 通道）。本文档以式中函数形态为主。

## 用法

### int POWER(int x, int y)
- `x`：底数，整数表达式。
- `y`：指数，整数表达式。
- 返回值：x 的 y 次幂（超出 Int64 范围 / NaN / 无穷大时报错）。
```erb
	A = POWER(2, 10)	; A = 1024
	A = POWER(3, 4)		; A = 81
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:78`（`["POWER"] = new PowerMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3086`（`PowerMethod`）

```text
构造：返回类型 = long；参数 = [long, long]；CanRestructure = true（参数全为常量时可折叠）。

GetIntValue(exm, args):
    x ← args[0].GetIntValue(exm)
    y ← args[1].GetIntValue(exm)
    pow ← Math.Pow(x, y)          ; 浮点幂运算
    若 pow 是 NaN:      抛出 CodeEE（"幂运算结果为非数值"）
    否则若 pow 是无穷大: 抛出 CodeEE（"幂运算结果为无穷大"）
    否则若 pow >= long.MaxValue 或 pow <= long.MinValue:
                        抛出 CodeEE（"幂运算结果超出 64 位符号整数范围"）
    返回 (long)pow
```

## 备注

- ecd/Command.md 的小节描述的是命令形态（结果赋给指定变量），ecd/Expression.md 只给出函数签名 `int POWER(int x, int y)`。两者对溢出的描述粒度不同：命令文档只说「溢出会出错」，源码细分 NaN / 无穷大 / Int64 超范围三种错误。
- 由于内部用浮点幂运算，极大数的精度受 double 尾数限制（如个位精度可能丢失），文档未提及。
- zh 套件未收录本函数。
