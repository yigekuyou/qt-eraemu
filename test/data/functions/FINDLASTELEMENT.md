# FINDLASTELEMENT

- **类别**：式中函数
- **签名**：int FINDLASTELEMENT(var array, ? value, int start = 0, int end = ※, int flag)
- **文档来源**：`ecd/Command.md`「### FINDLASTELEMENT `<一元数组>`, `<检索值>`, `<初始索引>`, `<终止索引>`, `<全词匹配>`」（FINDELEMENT 与 FINDLASTELEMENT 同节）；`ecd/Expression.md` 内置函数一览（`int FINDLASTELEMENT(var array, ? value, int start = 0, int end = ※, int flag)`）；zh 套件未收录

## 语义

在一元数组变量的指定范围内检索指定值，返回匹配元素的下标，赋值给返回值（指令形态时为 `RESULT:0`）。`FINDELEMENT` 为正向检索（返回第一个匹配项），`FINDLASTELEMENT` 为反向检索（返回最后一个匹配项）；未找到时返回 `-1`。

第 1 参数是要检索的数组变量，检索只发生在其第一维上。第 2 参数为要检索的值，类型须与数组元素一致。第 3、4 参数指定检索范围 `[start, end)`，缺省检索第一维全部元素。检索字符串时第 2 参数按 `REPLACE` 的规则解释为正则表达式；第 5 参数指定匹配精度，缺省 `0` 表示部分匹配，非 0 表示全词匹配。遇到第一个匹配项即终止。

## 用法

### int FINDLASTELEMENT(array, value)
- array：一元数组变量（数值或字符串）。
- value：检索值，类型与数组元素一致。
- 检索整个第一维，返回最后（最靠后）一个匹配下标，找不到返回 -1。
```erb
LOCALS = "abc", "def", "abc"
idx = FINDLASTELEMENT(LOCALS, "abc")   ; idx = 2
```
### int FINDLASTELEMENT(array, value, start, end)
- start、end：检索范围（左闭右开）。
```erb
idx = FINDLASTELEMENT(LOCALS, "abc", 0, 2)   ; 只在下标 0、1 中找
```
### int FINDLASTELEMENT(array, value, start, end, flag)
- flag：字符串检索时的匹配精度。0 = 部分匹配（正则命中即可），非 0 = 全词匹配（正则命中串长度须等于元素全长）。数值数组时该参数无效。
```erb
idx = FINDLASTELEMENT(LOCALS, "bc", 0, 3, 0)   ; 部分匹配可命中
idx = FINDLASTELEMENT(LOCALS, "bc", 0, 3, 1)   ; 全词匹配不命中（"abc" 长度 ≠ 2）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:104`（`["FINDLASTELEMENT"] = new FindElementMethod(true)`；`FINDELEMENT` 为同一类的 `false` 版本，`Runtime/Script/Statements/Function/Creator.cs:104`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3872`（`FindElementMethod`）；检索核心 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:367`（数值）/`:399`（字符串）

```text
FindElementMethod(isLast = true):
构造：返回类型 = long；
    参数表 [引用一元数组(可常量), 与第一参同型, int, int, int]，自第 3 参（OmitStart=2）起可省略；
    CanRestructure = true, HasUniqueRestructure = true；funcName = "FINDLASTELEMENT"。

GetIntValue(exm, args):
    isExact = false
    varTerm = (VariableTerm)args[0]          ; 第 1 参必须是数组变量项
    start = args.Count > 2 ? args[2] : 0
    end   = args.Count > 3 ? args[3] : varTerm.GetLength()   ; 缺省为第一维长度
    若 args.Count > 4: isExact = (args[4] != 0)
    p = varTerm.GetFixedVariableTerm(exm)
    p.IsArrayRangeValid(start, end, funcName, 3, 4)   ; 范围不合法 → CodeEE

    若第 1 参操作数类型为 long（数值数组）:
        targetValue = args[1].GetIntValue(exm)
        返回 VariableEvaluator.FindElement(p, targetValue, start, end, isExact, isLast)
    否则（字符串数组）:
        targetString = RegexFactory.GetRegex(args[1].GetStrValue(exm))
            ; 正则不合法 → CodeEE（第 2 参数正则错误）
        返回 VariableEvaluator.FindElement(p, targetString, start, end, isExact, isLast)

VariableEvaluator.FindElement(p, target: long, ...):
    若 start >= end: 返回 -1
    array = p.Identifier 为角色数据 ? GetArrayChara(p.Index1) : GetArray()   (long[])
    isLast: for i = end-1 downto start: 若 array[i] == target 返回 i
    返回 -1

VariableEvaluator.FindElement(p, target: Regex, ...):
    若 start >= end: 返回 -1
    array = ... (string[])
    isLast: for i = end-1 downto start:
        str = array[i] ?? ""          ; v1.823 起 null 当作空串
        若 isExact: match = target.Match(str)；命中且 str.Length == match.Length → 返回 i
        否则:       target.IsMatch(str) → 返回 i
    返回 -1
```

## 备注

- 文档将该函数连同 FINDELEMENT 以「指令」形式记载于 ecd/Command.md，实际在本仓库中是纯式中函数（FunctionMethod），调用结果作为表达式值而非写入 `RESULT:0`。
- Command.md 说「当检索字符串时……允许使用正则表达式」；源码证实字符串检索一律走 `RegexFactory.GetRegex`，数值检索走整数相等比较，`flag` 仅对字符串有意义。
- 数组为一元（1D）限制在源码中以 `argumentTypeArrayEx` 的 `ArgType.RefAny1D` 实现（旧的手写类型检查代码已被注释）。
- zh 套件未收录本函数。
