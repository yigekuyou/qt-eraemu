# LOG

- **类别**：式中函数
- **签名**：int LOG(int value)
- **文档来源**：`ecd/Expression.md` 签名列表（仅有签名 `int LOG(int value)`，无详解小节）

## 语义

计算参数的自然对数（底为 e），结果截断取整（转为 64 位整数）后返回。

参数必须为正整数：参数 ≤ 0 时抛出 CodeEE 运行期错误（"対数関数の引数に0以下の値が指定されました"，即对数函数的参数不能为 0 以下的值）。计算结果为 NaN、无穷大或超出 64 位符号整数范围时也抛出 CodeEE（自然对数下实际只会遇到参数为 0/负数的错误）。

注意返回值是整数：`LOG(10)` 返回 `2`（ln 10 ≈ 2.302…，截断为 2）。

## 用法

### int LOG(int value)
- `value`：整数表达式，必须 > 0。
- 返回值：ln(value) 截断取整。
```erb
A = LOG(10)    ; A = 2
A = LOG(1)     ; A = 0
A = LOG(0)     ; 抛出 CodeEE 错误
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:81`（`["LOG"] = new LogMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3148`（`LogMethod`，无参构造 `Base = Math.E`）

```text
构造：返回类型 = long；参数 = [long]；Base = Math.E；CanRestructure = true。

GetIntValue(exm, args):
    ret ← args[0].GetIntValue(exm)
    若 ret <= 0:
        抛出 CodeEE（Format(trerror.ArgIsNotMoreThan0, Name, 1, ret)）
    dret ← (double)ret
    若 Base == Math.E: dret ← Math.Log(dret)      ; 自然对数
    否则:              dret ← Math.Log10(dret)    ; LOG10 用同一实现类
    若 IsNaN(dret):        抛出 CodeEE（"計算値が非数値です"）
    若 IsInfinity(dret):   抛出 CodeEE（"計算値が無限大です"）
    若 dret ≥ long.MaxValue 或 ≤ long.MinValue:
        抛出 CodeEE（"計算結果が64ビット符号付き整数の範囲外です"）
    返回 (long)dret          ; 向零截断
```

## 备注

- ecd 文档只有一行签名，无语义详解；以上语义完全来自源码。"参数必须为正整数""结果截断取整"两点均未见于文档。
- 与 `LOG10` 共用同一个实现类：`LOG` 用 `Base = Math.E`，`LOG10` 用 `Base = 10.0`（见 `Runtime/Script/Statements/Function/Creator.cs:81`）。
- zh 套件未收录本函数。
