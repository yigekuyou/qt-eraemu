# ENUMMACROENDSWITH

- **类别**：式中函数（EM 私家版扩展，名字枚举函数族）
- **签名**：int ENUMMACROENDSWITH(str 后缀{, ref str 输出数组})
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:64-78`（组内「ENUMMACROENDSWITH str」）；两套中文文档未收录；第 2 参数为文档未记载的源码扩展

## 语义

枚举**已定义的宏名（`#DEFINE` 的名字）**中所有以 `<后缀>` 结尾的名字，返回**实际写入目标数组的个数**。

- 枚举对象 `GlobalStatic.IdentifierDictionary.MacroKeys`（`Runtime/Script/Data/IdentifierDictionary.cs:34`，宏关键字原样）。
- 后缀判定：`LastIndexOf(后缀) == 长度 - 后缀长`（`Runtime/Script/Statements/Function/Creator.Method.cs:204`）。
- 匹配**不区分大小写**；输出保持原样、**不排序**（`strs.Sort()` 被注释，`:211`）。
- 输出：第 2 参数（一维字符串数组变量）或省略时写 `RESULTS`（`RESULTS:0` 起）；返回值 = 写入个数，命中超出容量被截断（`RESULTS` 默认长度 100）。
- 空后缀返回 0。

## 用法

### int ENUMMACROENDSWITH(str 后缀)
```erb
#DEFINE CONFIG_DEBUG 0
#DEFINE CONFIG_RELEASE 1

PRINTFORML {ENUMMACROENDSWITH("_DEBUG")}     ;→ 1
PRINTFORML [{RESULTS:0}]                     ;→ [CONFIG_DEBUG]
```

### int ENUMMACROENDSWITH(str 后缀, ref str 输出数组)
```erb
#DIMS NAMES, 50
PRINTFORML {ENUMMACROENDSWITH("_DEBUG", NAMES)}   ;→ 写入个数
PRINTFORML [{NAMES:0}]
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:233`（`["ENUMMACROENDSWITH"] = new EnumNameMethod(EnumNameMethod.EType.Macro, EnumNameMethod.EAction.EndsWith)`）
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
            case EndsWith:
                if item.ToUpper().LastIndexOf(arg, Ordinal) == item.Length - arg.Length:
                    strs.Add(item)
    output = (args.Count == 2) ? args[1] 的字符串数组 : RESULTS 数组
    return Math.Min(output.Length, strs.Count)
```

## 备注

- 与 `ENUMMACROBEGINSWITH` 只差比较方向；枚举结果同样是「宏关键字」，不能用于动态求值。
- 结果顺序取决于 `macroDic` 的枚举顺序，**不要假设与 `#DEFINE` 书写顺序一致**（Dictionary 实现细节）。
