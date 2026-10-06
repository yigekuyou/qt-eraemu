# ENUMMACROWITH

- **类别**：式中函数（EM 私家版扩展，名字枚举函数族）
- **签名**：int ENUMMACROWITH(str 子串{, ref str 输出数组})
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:64-78`（组内「ENUMMACROWITH str」，注记「WITHはstrを含むシンボルです」）；两套中文文档未收录；第 2 参数为文档未记载的源码扩展

## 语义

枚举**已定义的宏名（`#DEFINE` 的名字）**中所有**包含** `<子串>` 的名字，返回**实际写入目标数组的个数**。

- 包含判定 `IndexOf(子串) >= 0`（`Runtime/Script/Statements/Function/Creator.Method.cs:207`）：前缀/后缀/中间命中都算。
- 枚举对象 `GlobalStatic.IdentifierDictionary.MacroKeys`（`Runtime/Script/Data/IdentifierDictionary.cs:34`）。
- 匹配**不区分大小写**；输出保持原样、**不排序**。
- 输出：第 2 参数（一维字符串数组变量）或省略时写 `RESULTS`（`RESULTS:0` 起）；返回值 = 写入个数（受容量限制，默认 100）。
- 空子串返回 0（源码 `if (arg.Length > 0)` 守卫——虽然空串在数学上包含于任何串）。

## 用法

### int ENUMMACROWITH(str 子串)
```erb
#DEFINE VAR_IS_NUM 1
#DEFINE VAR_IS_STRING 2
#DEFINE OTHER 3

PRINTFORML {ENUMMACROWITH("VAR_")}      ;→ 2
PRINTFORML {ENUMMACROWITH("IS_")}       ;→ 2（子串，不限于开头）
```

### int ENUMMACROWITH(str 子串, ref str 输出数组)
```erb
#DIMS NAMES, 50
PRINTFORML {ENUMMACROWITH("IS_", NAMES)}   ;→ 2（容量足够时即为命中数）
PRINTFORML [{NAMES:0}] [{NAMES:1}]
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:234`（`["ENUMMACROWITH"] = new EnumNameMethod(EnumNameMethod.EType.Macro, EnumNameMethod.EAction.With)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:151`（`EnumNameMethod`）

```text
构造（Creator.Method.cs:167-176）:
    返回类型 = long
    argumentTypeArrayEx = [{ String, RefString1D }, OmitStart = 1]
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:177-221）:
    arg = args[0].GetStrValue(exm).ToUpper()
    array = GlobalStatic.IdentifierDictionary.MacroKeys      ; Macro 类
    strs = []
    if arg.Length > 0:
        foreach item in array:
            if item.Length < arg.Length: continue
            case With:
                if item.ToUpper().IndexOf(arg, Ordinal) >= 0: strs.Add(item)
    output = (args.Count == 2) ? args[1] 的字符串数组 : RESULTS 数组
    return Math.Min(output.Length, strs.Count)
```

## 备注

- 三个 MACRO 版（`ENUMMACRO*`）与 FUNC/VAR 版共享实现，只有枚举源不同；EM readme 也把它们列在同一小节（`:64-78`）。
- 与 `ISDEFINED`（判断单个宏是否存在，`Emuera.EM_readme.txt:30-31`）互补：本函数用于「批量发现」，`ISDEFINED` 用于「精确确认」。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
