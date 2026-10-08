# INRANGEARRAY

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数
- **签名**：int INRANGEARRAY(var array, int min, int max, int start = 0, int end = ※)（※ 为数组末尾）
- **文档来源**：ecd/zh 两套文档均未收录本函数（`ecd/Expression.md` 签名列表中亦无）；语义依据源码推定

## 语义

统计一维整数数组变量中、指定范围内取值落在区间 `[min, max)`（大于等于 min 且**小于** max）的元素个数，返回到表达式中。

第 1 参数必须是数组变量（不能是二重/三重数组）；第 4、5 参数可省略，省略时分别默认 `0` 与数组长度。范围 `start～end` 超出数组边界时抛出 CodeEE 运行期错误（错误索引中的「INRANGECARRAY関数の範囲指定がキャラクタ配列の範囲を超えています」即其角色数组版本对应的错误）。

注意：与 `INRANGE`（单值判断，闭区间）不同，本函数的上界是**不含**的半开区间，这是源码行为，文档无记载。

## 用法

### int INRANGEARRAY(var array, int min, int max, int start = 0, int end = ※)
- `array`：一维整数数组变量（允许传常量数组的引用）。
- `min`：整数表达式，区间下界（含）。
- `max`：整数表达式，区间上界（**不含**）。
- `start`：整数表达式，起始下标，省略时为 `0`。
- `end`：整数表达式，结束下标（不含），省略时为数组长度。
- 返回值：范围内满足 `min <= 元素 < max` 的元素个数。
```erb
DIM A = 5, 10, 15, 20, 25
COUNT = INRANGEARRAY(A, 10, 20)      ; COUNT = 2（10 和 15；20 不小于 20，不计）
COUNT = INRANGEARRAY(A, 10, 21, 1)   ; COUNT = 2（从下标 1 起：15、20）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:106`（`["INRANGEARRAY"] = new InRangeArrayMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3986`（`InRangeArrayMethod`，无参构造版本）；核心循环在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:503`（`VariableEvaluator.GetInRangeArray`）

```text
构造：返回类型 = long；参数表 = [RefInt1D(允许常量引用), int, int, int, int]，从第 4 参数（下标 3）起可省略；
     CanRestructure = false。

GetIntValue(exm, args):
    min   ← args[1].GetIntValue(exm)
    max   ← args[2].GetIntValue(exm)
    varTerm ← args[0] 强转为 VariableTerm
    start ← args.Count > 3 且 args[3] 非空 ? args[3].GetIntValue(exm) : 0
    end   ← args.Count > 4 且 args[4] 非空 ? args[4].GetIntValue(exm) : varTerm.GetLength()
    p ← varTerm.GetFixedVariableTerm(exm)
    p.IsArrayRangeValid(start, end, "INRANGEARRAY", 4, 5)   ; 范围越界则抛 CodeEE
    返回 VariableEvaluator.GetInRangeArray(p, min, max, start, end)

GetInRangeArray(p, min, max, start, end):
    ret ← 0
    对 i = start .. end-1:
        value ← p.Identifier.GetIntValue(...)
        若 value >= min 且 value < max: ret++      ; 注意上界不含
    返回 ret
```

## 备注

- ecd/Command.md、ecd/Expression.md、zh 套件均未收录本函数，签名从源码参数表推定；`※` 默认值（数组长度）参照同族 `SUMARRAY` 的文档写法。
- 文档缺失期间需特别注意：本函数区间为 `[min, max)` 半开区间，而 `INRANGE` 为闭区间；若与官方 wiki 记载不一致，以本仓库源码为准。
- 角色变量版本为 `INRANGECARRAY`（同一个类，`isChara = true` 构造）。
