# INRANGECARRAY

- **类别**：式中函数
- **签名**：int INRANGECARRAY(var carray, int min, int max, int start = 0, int end = CHARANUM)
- **文档来源**：ecd/zh 两套文档均未收录本函数（`ecd/Expression.md` 签名列表中亦无）；语义依据源码推定，错误行为可交叉参考 `ecd/Error_Index.md`（INRANGECARRAY 範囲指定错误条目）

## 语义

统计角色变量（角色数组，按角色序号遍历）中、指定角色范围内取值落在区间 `[min, max)`（大于等于 min 且**小于** max）的元素个数，返回到表达式中。

第 1 参数必须是角色数据的整数数组变量；第 4、5 参数可省略，省略时分别默认 `0` 与 `CHARANUM`。`start` 或 `end` 为负、`start >= CHARANUM` 或 `end > CHARANUM` 时抛出 CodeEE 运行期错误（"INRANGECARRAY関数の範囲指定がキャラクタ配列の範囲を超えています"，即指定的范围超出了角色数组的范围）。

与 `INRANGE`（单值判断，闭区间）不同，本函数上界**不含**，这是源码行为，文档无记载。

## 用法

### int INRANGECARRAY(var carray, int min, int max, int start = 0, int end = CHARANUM)
- `carray`：角色数据的整数数组变量（如 `CFLAG` 等），允许传常量引用。
- `min`：整数表达式，区间下界（含）。
- `max`：整数表达式，区间上界（**不含**）。
- `start`：整数表达式，起始角色序号，省略时为 `0`。
- `end`：整数表达式，结束角色序号（不含），省略时为 `CHARANUM`。
- 返回值：角色范围内满足 `min <= 元素 < max` 的元素个数。
```erb
COUNT = INRANGECARRAY(CFLAG:2, 0, 100)          ; 全角色中 CFLAG:2 在 [0,100) 的人数
COUNT = INRANGECARRAY(CFLAG:2, 0, 50, 1, 5)     ; 仅统计角色 1～4
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:107`（`["INRANGECARRAY"] = new InRangeArrayMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3986`（`InRangeArrayMethod`，`isChara = true` 构造版本）；核心循环在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:518`（`VariableEvaluator.GetInRangeArrayChara`）

```text
构造：返回类型 = long；参数表 = [CharacterData | RefInt1D(允许常量引用), int, int, int, int]，
     从第 4 参数（下标 3）起可省略；isCharaRange = true；CanRestructure = false。

GetIntValue(exm, args):
    min   ← args[1].GetIntValue(exm)
    max   ← args[2].GetIntValue(exm)
    varTerm ← args[0] 强转为 VariableTerm
    start ← args.Count > 3 且 args[3] 非空 ? args[3].GetIntValue(exm) : 0
    end   ← args.Count > 4 且 args[4] 非空 ? args[4].GetIntValue(exm) : exm.VEvaluator.CHARANUM
    p ← varTerm.GetFixedVariableTerm(exm)
    charaNum ← exm.VEvaluator.CHARANUM
    若 start >= charaNum 或 start < 0 或 end > charaNum 或 end < 0:
        抛出 CodeEE（"INRANGECARRAY関数の範囲指定がキャラクタ配列の範囲を超えています(start～end)"）
    返回 VariableEvaluator.GetInRangeArrayChara(p, min, max, start, end)

GetInRangeArrayChara(p, min, max, start, end):
    ret ← 0
    对 i = start .. end-1:          ; i 为角色序号
        value ← p.Identifier.GetIntValue(..., [i, p.Index2, p.Index3])
        若 value >= min 且 value < max: ret++   ; 注意上界不含
    返回 ret
```

## 备注

- ecd/Command.md、ecd/Expression.md、zh 套件均未收录本函数；签名与默认值（`CHARANUM`）参照同族 `SUMCARRAY` 的文档写法与源码推定。
- `ecd/Error_Index.md:447` 收录了本函数的范围越界错误（错误级，出处 `Runtime/Script/Statements/Function/Creator.Method.cs`），与本仓库抛错行为对应。
- 与非角色版 `INRANGEARRAY` 共用同一个实现类，区别仅在于遍历维度（角色序号 vs 数组下标）与越界检查方式。
