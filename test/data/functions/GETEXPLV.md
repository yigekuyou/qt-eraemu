# GETEXPLV

- **类别**：式中函数
- **签名**：int GETEXPLV(int value, int maxLV)
- **文档来源**：`ecd/Command.md`「### GETEXPLV `<数值表达式>`, `<判断的 LV 上限>`」；`ecd/Expression.md`（表达式内函数签名列表）

## 语义

把给定值 `value` 与经验值等级阈值数组 `EXPLV` 逐级比较，返回该值至少达到了 `EXPLV` 的哪一级（0 ～ `maxLV`）。

第 2 参数 `maxLV` 表示要调查的最大 LV。使用前需先设置好 `EXPLV` 数组的值（`EXPLV:0` 为 LV1 所需经验，依此类推；实现中用 `EXPLV[i+1]` 与第 `i` 级比较）。

当 `value` 小于 `EXPLV:1` 时返回 0；超过所有已设阈值时返回 `maxLV`。

## 用法

### int GETEXPLV(int value, int maxLV)
- `value`：要判断的经验值（数值表达式）。
- `maxLV`：判断的 LV 上限。
- 返回值：`value` 至少达到的 `EXPLV` 等级（0 ～ maxLV）。
```erb
;前提：已用 REPLACE.CSV 或变量操作设置好 EXPLV
PRINTL 经验 250 对应的等级：{GETEXPLV(250, 10)}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:102`（`["GETEXPLV"] = new GetExpLVMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3846`（`GetExpLVMethod`）；辅助实现 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:77`（`getExpLv`）

```text
构造：返回类型 = long；参数 = [long, long]；CanRestructure = false。

GetIntValue(exm, args):
    value ← args[0].GetIntValue(exm)
    maxLv  ← args[1].GetIntValue(exm)
    返回 VEvaluator.getExpLv(value, maxLv)
    ; getExpLv:
    ;   for i = 0 .. maxlv-1:
    ;       若 value < varData 的 EXPLV 数组第 i+1 个元素:
    ;           返回 i
    ;   循环结束仍未返回则返回 maxlv
```

## 备注

- ecd/Command.md 以「赋值给 `RESULT:0`」的命令式口吻描述；实际注册形态是式中函数。
- 姊妹函数 `GETPALAMLV`（`Runtime/Script/Statements/Variable/VariableEvaluator.cs:70` 附近的 `getPalamLv`）逻辑完全相同，只是比较对象换成 `PALAMLV` 数组。
- 实现直接读取 `EXPLV` 数组的原始存储，不受变量作用域包装影响；文档要求"请先设置好 EXPLV 的值再使用"。
- zh 套件（Era-Chinese-Documentation）未收录本函数。
