# LIMIT

- **类别**：式中函数（另有同名命令形态）
- **签名**：int LIMIT(int value, int min, int max)
- **文档来源**：`ecd/Command.md`「### LIMIT `<数值表达式>`, `<数值表达式>`, `<数值表达式>`」；`ecd/Expression.md` 签名列表

## 语义

把第 1 参数的值夹在 `[min, max]` 区间内返回：value 小于 min 时返回 min，大于 max 时返回 max，否则返回 value 本身。

用于把表达式结果限制在一定范围内，替代手写的两段 `SIF` 判断，无副作用。

同名命令形态 `LIMIT <数值表达式>, <数值表达式>, <数值表达式>` 把结果赋值给 `RESULT:0`，语义与函数形态相同；本文档以式中函数形态为主。

## 用法

### int LIMIT(int value, int min, int max)
- `value`：整数表达式，被夹取的值。
- `min`：整数表达式，下界。
- `max`：整数表达式，上界。
- 返回值：`value < min` 时为 `min`；`value > max` 时为 `max`；否则为 `value`。
```erb
; 把 X - Y 赋值给 A，且保证 0 ≤ A ≤ 100，两行完成：
A = LIMIT(X - Y, 0, 100)

; 命令形态（结果进 RESULT:0）：
LIMIT X - Y, 0, 100
A = RESULT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:85`（`["LIMIT"] = new GetLimitMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3234`（`GetLimitMethod`）

```text
构造：返回类型 = long；参数 = [long, long, long]；CanRestructure = true（参数均为常量时可折叠）。

GetIntValue(exm, args):
    value ← args[0].GetIntValue(exm)
    min   ← args[1].GetIntValue(exm)
    max   ← args[2].GetIntValue(exm)
    若 value < min:  返回 min
    若 value > max:  返回 max
    否则:            返回 value
```

## 备注

- ecd/Command.md 给出的示例正是本函数的典型用途（把 `SIF` 判断压缩为两行）；函数形态与命令形态语义一致，仅结果去向不同（返回值 vs `RESULT:0`）。
- 源码不校验 min ≤ max；min > max 时行为为"先比 min 再比 max"（此时任何 value 都会被夹成 min 或 max 之一），文档未提及该边界。
- zh 套件未收录本函数。
