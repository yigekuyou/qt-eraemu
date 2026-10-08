# GETPALAMLV

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（ecd/Command.md 以命令口径收录；本仓库仅有函数形态）
- **签名**：int GETPALAMLV(int value, int maxLV)
- **文档来源**：`ecd/Command.md`「### GETPALAMLV `<数值表达式>`, `<判断的 LV 上限>`」；`ecd/Expression.md`（表达式内函数签名列表）；zh 套件未收录

## 语义

把给定值 `value` 与 `PALAMLV` 变量的各级阈值比较，返回该值至少达到了 `PALAMLV` 的哪一级。

第 2 参数 `maxLV` 表示要调查的最大 LV。请先设置好 `PALAMLV` 的值再使用（通常在游戏初始化时由 `PALAMLV` 相关设置或 eramakerBase 的 `PALAMLV` 变量初始化决定）。

判定规则（源码语义）：从 LV0 开始逐级检查，当 `value < PALAMLV[i+1]` 时返回 `i`；若直到最后都不小于阈值，返回 `maxLV`。即返回值范围为 `0 ~ maxLV`。

## 用法

### int GETPALAMLV(int value, int maxLV)
- `value`：整数表达式，被判断的值（通常是某一参数的当前值）。
- `maxLV`：整数表达式，要调查的最大 LV。
- 返回值：`value` 至少达到的 `PALAMLV` 级别（0 ~ maxLV）。
```erb
;设 PALAMLV:1 = 100, PALAMLV:2 = 500, …（PALAMLV:0 为 LV0 阈值）
A = GETPALAMLV(PALAM:欲望, 8)
PRINTFORML 欲望 LV{A}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:101`（`["GETPALAMLV"] = new GetPalamLVMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3820`（`GetPalamLVMethod`）；转调 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:67`（`VariableEvaluator.getPalamLv`）

```text
构造：返回类型 = long；参数 = [long, long]；CanRestructure = false。

GetIntValue(exm, args):
    value ← args[0].GetIntValue(exm)
    maxLv  ← args[1].GetIntValue(exm)
    返回 exm.VEvaluator.getPalamLv(value, maxLv)

getPalamLv(pl, maxlv):                        # VariableEvaluator.cs:67
    对 i = 0 .. maxlv-1:
        若 pl < varData.DataIntegerArray[PALAMLV][i + 1]:   # 注意比较的是 PALAMLV[i+1]
            返回 i
    返回 maxlv
```

## 备注

- ecd/Command.md 的小节按"赋值给 `RESULT:0`"的命令口径描述；本仓库 `Runtime/Script/Statements/FunctionIdentifier.cs` 中没有 GETPALAMLV 命令注册，只有式中函数形态。
- 源码循环比较的是 `PALAMLV[i+1]`（即 LV1 ~ LVmax 的阈值），`PALAMLV:0` 不参与判定；`value` 不小于所有受检阈值时返回 `maxlv`。文档只说"与 PALAMLV 比较"，未写明这一细节。
- 文档建议"先设置好 PALAMLV 的值再使用"；源码不做边界校验，`maxlv` 大于 PALAMLV 数组实际长度时会数组越界（运行期错误）。
- 姊妹函数 `GETEXPLV`（对 `EXPLV` 做同样判定）实现完全同构，见 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:77`。
- zh 套件未收录本函数。
