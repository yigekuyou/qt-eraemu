# REPLACE

- **类别**：式中函数（另有同名命令形态）
- **签名**：str REPLACE(str source, str match, str newvalue)
- **签名**：str REPLACE(str source, str match, str refArray, int type)
- **文档来源**：`ecd/Expression.md`（仅 3 参数签名）；`ecd/Command.md`「### REPLACE `<原始字符串>`, `<查找字符串>`, `<替换字符串>`」（命令形态）；4 参数扩展无文档（源码注释「EM_私家版_REPLACE拡張」）

## 语义

字符串替换函数。在第 1 参数的原始字符串中查找匹配，命中后替换并作为函数值返回。

基础形态（3 参数）：第 2 参数按正则表达式解释（遵循 C# 正则规范），命中部分替换为第 3 参数。因此 `(`、`)`、`[`、`]`、`$`、`.`、`*`、`+` 等正则元字符必须转义（可配合 `ESCAPE` 函数）。第 2 参数不是合法正则表达式时抛出 CodeEE。

扩展形态（4 参数，EM 私家版扩展，文档未收录），第 4 参数 type 决定行为：
- `type = 1`：第 3 参数须是一个一维字符串数组的引用，按匹配出现的顺序依次用数组各元素替换；数组元素用尽后替换为空字符串。
- `type = 2`：不使用正则表达式，进行普通的字面替换（第 2、3 参数按普通字符串处理）。
- 其他值（含省略为 0）：等同 3 参数形态的正则替换。

同名命令形态 `REPLACE <原始字符串>, <查找字符串>, <替换字符串>` 把替换结果赋值给 `RESULTS`（ecd/Command.md 有独立小节）。本文档以式中函数形态为主。

## 用法

### str REPLACE(str source, str match, str newvalue)
- `source`：原始字符串。
- `match`：查找用的正则表达式模式。
- `newvalue`：替换文本（可用正则替换引用 `$1` 等）。
- 返回值：替换后的字符串。
```erb
	STR = REPLACE("Hello, World", "o", "0")	; "Hell0, W0rld"
	STR = REPLACE("abc", @"a.c", "X")		; "X"（正则匹配）
```
### str REPLACE(str source, str match, str refArray, int type)
- `source`：原始字符串。
- `match`：查找模式（见 type 语义）。
- `refArray`：一维字符串数组引用（仅 type = 1 时使用，按顺序取元素替换）。
- `type`：`1` = 依次用数组元素替换；`2` = 不用正则的普通替换；其他 = 正则替换。
- 返回值：替换后的字符串。
```erb
	STR = REPLACE("a-b-c", "-", "/", 2)		; "a/b/c"（普通替换）
	STR = REPLACE("a,b,c", "b", LOCALS, 1)	; 用 LOCALS 的元素依次替换
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:127`（`["REPLACE"] = new ReplaceMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4599`（`ReplaceMethod`，EM_私家版_REPLACE拡張）

```text
构造：返回类型 = string；两种参数列表：
  ① [String, String, String, Int]，第 3 参数起可省略（即 3 或 4 参数）；
  ② [String, String, RefString1D|AllowConstRef, Int]（第 3 参数为一维字符串数组引用）；
HasUniqueRestructure = true、CanRestructure = false（第 4 参数为非 1 时可在编译期重排）。

GetStrValue(exm, args):
    baseString ← args[0].GetStrValue(exm)
    type ← (args.Count == 4) ? (int)args[3].GetIntValue(exm) : 0
    若 type != 2:
        try: reg ← RegexFactory.GetRegex(args[1].GetStrValue(exm))
        catch ArgumentException: 抛出 CodeEE（"第 2 参数不是合法正则表达式"）
    若 args.Count == 4:
        switch type:
        case 1:
            若 args[2] 不是变量项，或它是计算型/非一维数组/非字符串/常量:
                抛出 CodeEE（"第 3 参数不是一维字符串数组"）
            items ← 该数组引用指向的 string[]
            idx ← 0
            返回 reg.Replace(baseString, match =>
                idx < items.Length ? items[idx++] : "")   ; 数组用尽后替换为空串
        case 2:
            返回 baseString.Replace(args[1].GetStrValue(exm),
                                    args[2].GetStrValue(exm))   ; 普通字符串替换
    ; type == 0、> 2 或第 4 参数省略时
    返回 reg.Replace(baseString, args[2].GetStrValue(exm))
```

## 备注

- ecd/Command.md 命令小节说「内部处理完全使用正则表达式」；这对 3 参数形态成立，但源码的 4 参数 `type = 2` 扩展恰恰绕过正则做普通替换——文档未覆盖该扩展（源码注释标明为 EM 私家版扩展）。
- ecd/Expression.md 只收录 3 参数签名，4 参数扩展签名仅在源码 `argumentTypeArrayEx` 中定义。
- `RegexFactory.GetRegex` 带 Emuera 自己的正则缓存；替换文本第 3 参数在正则形态下会被 .NET `Regex.Replace` 解释 `$1` 等替换模式，文档未提及。
- zh 套件未收录本函数。
