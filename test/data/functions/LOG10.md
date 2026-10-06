# LOG10

- **类别**：式中函数
- **签名**：int LOG10(int value)
- **文档来源**：`ecd/Expression.md` 签名列表（仅有签名 `int LOG10(int value)`，无详解小节）

## 语义

计算参数的常用对数（底为 10），结果截断取整（转为 64 位整数）后返回。

参数必须为正整数：参数 ≤ 0 时抛出 CodeEE 运行期错误（对数函数的参数不能为 0 以下的值）。计算结果为 NaN、无穷大或超出 64 位符号整数范围时也抛出 CodeEE。

注意返回值是整数：`LOG10(100)` 返回 `2`（log10 100 = 2），`LOG10(999)` 同样返回 `2`（≈2.9996 截断）。

## 用法

### int LOG10(int value)
- `value`：整数表达式，必须 > 0。
- 返回值：log10(value) 截断取整。
```erb
A = LOG10(100)   ; A = 2
A = LOG10(1000)  ; A = 3
A = LOG10(-1)    ; 抛出 CodeEE 错误
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:82`（`["LOG10"] = new LogMethod(10.0d)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3148`（`LogMethod`，`Base = 10.0` 构造）

```text
构造：返回类型 = long；参数 = [long]；Base = 10.0；CanRestructure = true。

GetIntValue(exm, args):
    ret ← args[0].GetIntValue(exm)
    若 ret <= 0:
        抛出 CodeEE（Format(trerror.ArgIsNotMoreThan0, Name, 1, ret)）
    dret ← (double)ret
    因 Base(10.0) != Math.E: dret ← Math.Log10(dret)
    若 IsNaN(dret):        抛出 CodeEE（"計算値が非数値です"）
    若 IsInfinity(dret):   抛出 CodeEE（"計算値が無限大です"）
    若 dret ≥ long.MaxValue 或 ≤ long.MinValue:
        抛出 CodeEE（"計算結果が64ビット符号付き整数の範囲外です"）
    返回 (long)dret          ; 向零截断
```

## 备注

- ecd 文档只有一行签名，无语义详解；以上语义完全来自源码（与 `LOG` 共用 `LogMethod` 类，仅底数不同）。
- 由于是 double 运算再截断，存在理论上的精度边界（如底数恰好为 10 的幂时若 Math.Log10 返回 1.9999… 会被截为 1）；文档未提及，实测以运行环境为准。
- zh 套件未收录本函数。
