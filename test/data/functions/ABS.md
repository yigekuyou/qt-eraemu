# ABS

- **类别**：式中函数（另有同名命令形态）
- **签名**：int ABS(int n)
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；`ecd/Command.md`「### ABS `<数值表达式>`」（命令形态）

## 语义

返回参数的绝对值。参数为一个整数表达式；结果为该值的绝对值（int/long）。

特殊限制：当参数值为 `long.MinValue`（-9223372036854775808）时，因取绝对值会溢出，抛出 CodeEE 运行期错误，提示不能对 64 位最小值使用 ABS。

同名命令形态 `ABS <数值表达式>` 把参数的绝对值赋值给 `RESULT:0`，语义与函数形态相同（ecd/Command.md 有独立小节）。本文档以式中函数形态为主。

## 用法

### int ABS(int n)
- `n`：整数表达式，求其绝对值。
- 返回值：`n` 的绝对值。
```erb
A = ABS(-5)      ; A = 5
A = ABS(A)       ; 常用于把变量变为非负值
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:77`（`["ABS"] = new AbsMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3068`（`AbsMethod`）

```text
构造：返回类型 = long；参数 = [long]；CanRestructure = true（常量折叠允许）。

GetIntValue(exm, args):
    ret ← args[0].GetIntValue(exm)
    若 ret == long.MinValue:
        抛出 CodeEE（"不能对 int64 的最小值应用 ABS"）
    返回 Math.Abs(ret)
```

## 备注

- ecd/Command.md 中的小节描述的是命令形态（结果赋给 `RESULT:0`）；ecd/Expression.md 只在签名列表中给出 `int ABS(int n)`。两者语义一致。
- 源码对 `long.MinValue` 特判抛错，文档未提及该边界行为。
- zh 套件（Era-Chinese-Documentation）未收录本函数。
