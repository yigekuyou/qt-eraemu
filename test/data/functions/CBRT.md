# CBRT

- **类别**：式中函数
- **签名**：`int CBRT(int value)`
- **文档来源**：`ecd/Expression.md`（内置函数目录「その他の関数」附近签名列表）；zh 套件未收录

## 语义

求实数立方根（cube root），返回 `value^(1/3)` 的整数部分（结果截断为整型）。是 `SQRT` 的立方根版。

参数为负值时抛出运行时错误（CodeEE）终止脚本，不做复数立方根处理。返回值直接作为表达式结果使用（不是写入 RESULT 的命令）。

## 用法

### int CBRT(int value)
- `value`：要求立方根的非负整数。负值报错。
```erb
; 27 的立方根为 3.0，截断为 3
PRINTFORML CBRT(27) = {CBRT(27)}      ; → 3
; 26 的立方根约 2.962，截断为 2（不是四舍五入）
PRINTFORML CBRT(26) = {CBRT(26)}      ; → 2
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:80`（`["CBRT"] = new CbrtMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3130`（`CbrtMethod`，返回类型 long，参数 1 个 long，`CanRestructure = true`）

```text
函数 CBRT(value):
    ret ← value 求值（整数）
    若 ret < 0:
        抛出 CodeEE（"CBRT関数の引数に負の値が指定されました" 之类的参数为负错误，
                    错误信息带函数名、第 1 参数、实际值）
    返回 (long) Math.Pow(ret, 1.0 / 3.0)
```

## 备注

- 文档签名 `int CBRT(int value)` 与源码一致（内部按 long 计算，返回 long）。
- 实现用 `Math.Pow(ret, 1/3)` 而非 `Math.Cbrt`，浮点误差可能导致如 CBRT(64) 得到 3.999…→3 的结果；对完全立方数通常无碍，边界值需注意。
- zh 套件（Era-Chinese-Documentation）未收录该函数。
