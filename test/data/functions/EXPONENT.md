# EXPONENT

- **类别**：式中函数
- **签名**：int EXPONENT(int value)
- **文档来源**：`ecd/Expression.md`「内置表达式内函数一览」`int EXPONENT(int value)`；`ecd/Command.md` 未单独收录小节；zh 套件未收录

## 语义

计算自然指数 e 的 `value` 次幂（e^value），把结果截断为 64 位整数返回。即源码中的 `Math.Exp(value)`。函数名虽含「exponent」，语义是自然指数函数（与 `LOG` 互为反函数），并非求指数位数。

错误行为（任一命中即抛出 CodeEE 并中断脚本）：
- 计算结果为 NaN：「{0}関数: 計算結果が非数値です」（`EXPONENT` 的参数是整数，实际很难触发）。
- 结果为无穷大：「{0}関数: 計算結果が無限大です」。
- 结果超出 64 位有符号整数范围：「{0}関数: 計算結果({1})が64ビット符号付き整数の範囲外です」——e^709 之后的溢出即报错。

## 用法

### int EXPONENT(int value)
- `value`：指数（整数）。
- 返回值：e^value 截断为整数（如 `EXPONENT(0)` = 1，`EXPONENT(1)` = 2，`EXPONENT(2)` = 7）。
```erb
PRINTV EXPONENT(0)     ; 输出 1
PRINTV EXPONENT(1)     ; 输出 2 (e ≈ 2.71828 截断)
PRINTV EXPONENT(5)     ; 输出 148
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:83`（`["EXPONENT"] = new ExpMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3192`（`ExpMethod`）

```text
ExpMethod:
构造：返回类型 = long；参数表 = [long]（1 个参数）；CanRestructure = true。
GetIntValue(exm, args):
    ret ← args[0].GetIntValue(exm)
    dret ← Math.Exp(ret)            ; e^ret（double）
    若 dret 为 NaN:      抛出 CodeEE("{0}関数: 計算結果が非数値です")
    否则若 dret 为 ±∞:   抛出 CodeEE("{0}関数: 計算結果が無限大です")
    否则若 dret >= long.MaxValue 或 dret <= long.MinValue:
        抛出 CodeEE("{0}関数: 計算結果({1})が64ビット符号付き整数の範囲外です")
    返回 (long)dret                   ; 向零截断
```

## 备注

- ecd/Command.md 未给 EXPONENT 独立小节，仅有 Expression.md 目录中的签名行，语义完全依据源码（`Math.Exp`）。
- 与同组 `LOG`/`LOG10`（`LogMethod`）共用同一套 NaN/∞/Int64 溢出检查结构。
- 注意：由于返回类型是整数，`EXPONENT(1)` 得 2 而非 e；需要小数精度时应先放大再缩放。
