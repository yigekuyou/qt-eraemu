# INRANGE

- **类别**：式中函数
- **签名**：int INRANGE(int value, int min, int max)
- **文档来源**：`ecd/Command.md`「### INRANGE `<数值表达式>`, `<数值表达式>`, `<数值表达式>`」；`ecd/Expression.md` 签名列表

## 语义

判断第 1 参数是否落在闭区间 `[min, max]` 内：第 1 参数大于等于第 2 参数且小于等于第 3 参数时返回 `1`，小于第 2 参数或大于第 3 参数时返回 `0`。

纯计算函数，无副作用，参数均为整数表达式，可在任意表达式中使用。

## 用法

### int INRANGE(int value, int min, int max)
- `value`：整数表达式，被判断的值。
- `min`：整数表达式，区间下界（含）。
- `max`：整数表达式，区间上界（含）。
- 返回值：`min <= value <= max` 时为 `1`，否则为 `0`。
```erb
IF INRANGE(X, 0, 100)
  PRINTL X 在 0～100 之间
ENDIF
A = INRANGE(Y, 1, 9)   ; A = 0 或 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:105`（`["INRANGE"] = new InRangeMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3969`（`InRangeMethod`）

```text
构造：返回类型 = long；参数 = [long, long, long]；CanRestructure = true。

GetIntValue(exm, args):
    value ← args[0].GetIntValue(exm)
    min   ← args[1].GetIntValue(exm)
    max   ← args[2].GetIntValue(exm)
    若 (value >= min) 且 (value <= max) 返回 1，否则返回 0
```

## 备注

- ecd/Command.md 小节与源码一致（上界含于区间，即闭区间判断）。
- 注意与数组版本 `INRANGEARRAY`/`INRANGECARRAY` 的区别：数组版本统计元素个数时使用的是**半开区间**（`min <= 值 < max`，见 VariableEvaluator.GetInRangeArray），与本函数的闭区间语义不同。
- zh 套件未收录本函数。
