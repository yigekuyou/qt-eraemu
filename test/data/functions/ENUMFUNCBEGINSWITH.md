# ENUMFUNCBEGINSWITH

- **类别**：式中函数（EM 私家版扩展，名字枚举函数族）
- **签名**：int ENUMFUNCBEGINSWITH(str 前缀{, ref str 输出数组})
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:64`「◆ int ENUMFUNCBEGINSWITH str」（与其它 8 个 ENUM 函数成组记载，`:64-78`）；两套中文文档（`ecd/`、`_extracted/zh/`）未收录；第 2 参数（输出数组）为文档未记载的源码扩展

## 语义

枚举**已定义的非事件标签名（函数名）**中所有以 `<前缀>` 开头的名字，返回**实际写入目标数组的个数**。

- 「非事件标签」（`Process.LabelDictionary.NoneventKeys`，`Runtime/Script/Data/LabelDictionary.cs:15`）= 普通 `@标签` 与 `#FUNCTION`/`#FUNCTIONS` 函数名；被识别为事件函数的标签（`EVENTFIRST` 等）不在其中（`Runtime/Script/Data/LabelDictionary.cs:71-81`）。
- 匹配**不区分大小写**：源码把模式与候选都 `ToUpper()` 后比较（`Runtime/Script/Statements/Function/Creator.Method.cs:179, 201`）；输出的名字保持字典中的原样（未排序）。
- **顺序不保证**：源码里 `strs.Sort()` 被注释掉（`Runtime/Script/Statements/Function/Creator.Method.cs:211`），结果顺序即字典键的枚举顺序（.NET 实现细节，通常接近定义/插入顺序）。
- 输出位置：给出第 2 参数 → 写入该一维字符串数组；省略 → 写入系统字符串数组 `RESULTS`（`RESULTS:0` 起）。
- 返回值是**写入个数** `min(目标数组长度, 命中数)`，不是命中总数——若命中数超过数组容量（`RESULTS` 默认长度 100，`Runtime/Script/Data/ConstantData.cs:189-190`），多出的名字被丢弃且返回值小于命中数。这与 EM readme 的「総数を返します（返回总数）」表述有出入（见备注）。
- 空字符串前缀**不匹配任何名字**（源码有 `if (arg.Length > 0)` 守卫，`Runtime/Script/Statements/Function/Creator.Method.cs:194`），返回 0——而不是「枚举全部」。
- 名字长度小于前缀长度者直接跳过（`Runtime/Script/Statements/Function/Creator.Method.cs:197`），属性能优化，不影响语义。

## 用法

### int ENUMFUNCBEGINSWITH(str 前缀)（结果写入 RESULTS）
```erb
@COM_GREET
#FUNCTION
	RETURNF 1
@OTHER
#FUNCTION
	RETURNF 0

PRINTFORML {ENUMFUNCBEGINSWITH("COM_")}     ;→ 1
PRINTFORML [{RESULTS:0}]                    ;→ [COM_GREET]
```

### int ENUMFUNCBEGINSWITH(str 前缀, ref str 输出数组)
```erb
#DIMS NAMES, 50
PRINTFORML {ENUMFUNCBEGINSWITH("COM_", NAMES)}   ;→ 写入个数
PRINTFORML [{NAMES:0}]
PRINTFORML {ENUMFUNCBEGINSWITH("")}              ;→ 0（空前缀不枚举）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:226`（`["ENUMFUNCBEGINSWITH"] = new EnumNameMethod(EnumNameMethod.EType.Function, EnumNameMethod.EAction.BeginsWith)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:151`（`EnumNameMethod`；枚举 `EType`/`EAction` 于 `:153-164`）
- 名字来源：`Runtime/Script/Data/LabelDictionary.cs:15`（`NoneventKeys`）

```text
构造（Creator.Method.cs:167-176）:
    返回类型 = long
    argumentTypeArrayEx = [{ String, RefString1D }, OmitStart = 1]
    ; 第 1 参（模式）必填；第 2 参必须是一维字符串数组变量，可省略
    CanRestructure = false
    ; 9 个 ENUM 函数共用本类，按 type(Function/Variable/Macro) 与 action 分支

GetIntValue(exm, args)（Creator.Method.cs:177-221）:
    arg = args[0].GetStrValue(exm).ToUpper()
    array = switch type:
        Function → GlobalStatic.Process.LabelDictionary.NoneventKeys
        Variable → GlobalStatic.IdentifierDictionary.VarKeys
        Macro    → GlobalStatic.IdentifierDictionary.MacroKeys
    strs = []
    if arg.Length > 0:                       ; 空模式 → 不枚举，返回 0
        foreach item in array:
            if item.Length < arg.Length: continue
            case BeginsWith:
                if item.ToUpper().IndexOf(arg, Ordinal) == 0: strs.Add(item)
            ; EndsWith → LastIndexOf(arg) == item.Length - arg.Length
            ; With     → IndexOf(arg) >= 0
    output = (args.Count == 2) ? (args[1] as VariableTerm).Identifier.GetArray() as string[]
                               : exm.VEvaluator.RESULTS_ARRAY
    ret = strs.ToArray()
    outputlength = Math.Min(output.Length, ret.Length)
    Array.Copy(ret, output, outputlength)    ; 超容量部分丢弃
    return outputlength                      ; 返回写入个数
```

## 备注

- 文档与源码差异：EM readme 只写 1 个参数、并称「総数を返します」；源码支持可选的第 2 参输出数组，返回值为**写入数**（受目标容量限制）。当命中数超过 100（`RESULTS` 默认容量）时两者不等。
- 源码里 `// strs.Sort();` 被注释掉 → 结果**不排序**；若依赖稳定顺序（如与另一份结果按下标配对），应自行排序或改用 `ARRAYMSORT` 之类手段。
- 与变量版（`ENUMVARBEGINSWITH`）、宏版（`ENUMMACROBEGINSWITH`）同源，仅枚举对象不同。
