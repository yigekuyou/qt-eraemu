# FINDELEMENT

- **类别**：式中函数
- **签名**：int FINDELEMENT(var array, ? value, int start = 0, int end = ※, int flag)
- **文档来源**：`ecd/Command.md`「### FINDELEMENT `<一元数组>`, `<检索值>`, `<初始索引>`, `<终止索引>`, `<全词匹配>`」小节；`ecd/Expression.md`「内置表达式内函数一览」`int FINDELEMENT(var array, ? value, int start = 0, int end = ※, int flag)`；zh 套件未收录

## 语义

在数组变量 `array` 的第一维 `[start, end)` 范围内检索等于 `value` 的元素，返回其索引；未找到返回 `-1`。`FINDELEMENT` 正向（从头向尾）检索，命中第一个即返回；`FINDLASTELEMENT` 反向检索。

- 第 1 参数必须是一维数组变量（引用型常量数组亦可），不接受二维/三维数组。
- 第 2 参数类型与数组元素一致。检索**字符串**时允许使用正则表达式（与 `REPLACE` 类似遵循 C# 正则规范）；检索数值时忽略 `flag`。
- 第 3、4 参数指定检索范围，默认检索第一维全部元素。
- 第 5 参数 `flag` 指定字符串检索精度：默认（省略或 0）为部分匹配，非 0 为全词匹配（正则命中串长度 == 原串长度才视为命中）。字符串元素为 null 时按空字符串处理。

错误行为：
- 第 2 参数正则表达式语法非法：抛出 CodeEE「{0}関数: 第{1}引数が正規表現として不正です: {2}」。
- `start`/`end` 超出数组第一维范围：抛出 CodeEE「"FINDELEMENT"命令の第{1}引数({2})は配列"{3}"の範囲外です」。
- `start >= end` 时不报错，直接返回 `-1`。

## 用法

### int FINDELEMENT(var array, ? value, int start = 0, int end = ※, int flag)
- `array`：被检索的一维数组变量。
- `value`：检索值（数值或字符串/正则模式）。
- `start`：起始索引，默认 0。
- `end`：终止索引（不含），默认第一维长度。
- `flag`：0（省略）= 部分匹配，非 0 = 全词匹配（仅对字符串检索有效）。
- 返回值：命中的元素索引，未找到为 `-1`。
```erb
; 在 STR 数组中找含 "abc" 的元素（部分匹配）
I = FINDELEMENT(STRS, "abc")
; 全词匹配查找等于 "abc" 的元素，范围 [0, 10)
I = FINDELEMENT(STRS, "abc", 0, 10, 1)
; 正则查找以 "旗" 开头的元素
I = FINDELEMENT(STRS, "^旗")
; 数值数组
I = FINDELEMENT(DA, 100)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:103`（`["FINDELEMENT"] = new FindElementMethod(false)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3872`（`FindElementMethod`，`isLast = false`，`funcName = "FINDELEMENT"`）
- 转调：`Runtime/Script/Statements/Variable/VariableEvaluator.cs:367`（数值版 `FindElement`）、`:399`（正则版 `FindElement`）；范围校验 `Runtime/Script/Statements/Variable/VariableToken.cs:288`（`IsArrayRangeValid`）

```text
FindElementMethod:
构造：返回类型 = long；
     参数表 = [一维引用数组(可含常量数组), 与第1参数同型, int, int, int]
             （第 3、4、5 参数可省略）；
     CanRestructure = true 且 HasUniqueRestructure（const 数组+常量检索值可折叠）；
     isLast = false。
GetIntValue(exm, args):
    isExact ← false
    start ← (args.Count > 2 且 args[2] != null) ? args[2] : 0
    end   ← (args.Count > 3 且 args[3] != null) ? args[3] : varTerm.GetLength()
    若 args.Count > 4 且 args[4] != null: isExact ← (args[4] != 0)
    p ← varTerm.GetFixedVariableTerm(exm)
    p.IsArrayRangeValid(start, end, funcName, 3, 4)
        ; 校验 0 ≤ start, end ≤ 数组第一维大小，越界抛
        ; CodeEE(""FINDELEMENT"命令の第{1}引数({2})は配列"{3}"の範囲外です")
    若 args[0] 为数值数组:
        targetValue ← args[1].GetIntValue(exm)
        返回 VariableEvaluator.FindElement(p, targetValue, start, end, isExact, isLast)
    否则（字符串数组）:
        try targetString ← RegexFactory.GetRegex(args[1].GetStrValue(exm))
        catch ArgumentException: 抛出 CodeEE("{0}関数: 第{1}引数が正規表現として不正です: {2}")
        返回 VariableEvaluator.FindElement(p, targetString, start, end, isExact, isLast)

VariableEvaluator.FindElement(p, target:long, start, end, isExact, isLast):
    若 start >= end: 返回 -1
    array ← p 为角色变量 ? GetArrayChara(p.Index1) : GetArray()   ; long[]
    若 isLast: 对 i 从 end-1 降到 start: 若 target == array[i]: 返回 i
    否则:      对 i 从 start 到 end-1:  若 target == array[i]: 返回 i
    返回 -1

VariableEvaluator.FindElement(p, target:Regex, start, end, isExact, isLast):
    若 start >= end: 返回 -1
    array ← p 为角色变量 ? GetArrayChara(p.Index1) : GetArray()   ; string[]
    对每个 i（isLast 反向 / 正向）:
        str ← array[i] ?? ""                ; null 按空字符串处理
        若 isExact: match ← target.Match(str); 若 match.Success 且 str.Length == match.Length: 返回 i
        否则:       若 target.IsMatch(str): 返回 i
    返回 -1
```

## 备注

- ecd/Command.md 以命令口吻称结果「返回索引到 RESULT:0 中」，本函数实际为式中函数，索引直接作为表达式的值返回；Expression.md 的签名与之语义一致。
- Expression.md 把第 5 参数写作必填的 `int flag`，源码 `OmitStart = 2` 实际允许省略（省略时按部分匹配），Command.md 也写明「默认值为 0」；以源码为准。
- 数值检索时 `isExact` 被完全忽略（数值只有相等一种判定），`flag` 只对字符串（正则）检索有意义。
- 1823 版起（源码注释「1823 Nullなら空文字列として扱う」）字符串数组中的 null 元素按空字符串参与匹配。
- 支持对角色变量（如 `CSTR` 的某角色行）检索：第 1 参数为角色变量时沿其第一维（即角色内的元素维）检索，见 `GetArrayChara(p.Index1)`。
