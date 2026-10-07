# ENUMMACROBEGINSWITH

- **类别**：式中函数（EM 私家版扩展，名字枚举函数族）
- **签名**：int ENUMMACROBEGINSWITH(str 前缀{, ref str 输出数组})
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:64-78`（组内「ENUMMACROBEGINSWITH str」，注记「MACROはマクロ名」）；两套中文文档未收录；第 2 参数为文档未记载的源码扩展

## 语义

枚举**已定义的宏名（`#DEFINE` 的名字）**中所有以 `<前缀>` 开头的名字，返回**实际写入目标数组的个数**。

- 枚举对象是 `GlobalStatic.IdentifierDictionary.MacroKeys`（`Runtime/Script/Data/IdentifierDictionary.cs:34` → `macroDic.Values.Select(v => v.Keyword).ToArray()`），即每条 `#DEFINE` 记录的关键字；宏名按定义原样保存。
- 宏带参数时（`#DEFINE FOO(A,B)`）关键字仍是 `FOO`，本函数枚举的是关键字而非展开结果。
- 匹配**不区分大小写**（两侧 `ToUpper()`，`Runtime/Script/Statements/Function/Creator.Method.cs:179, 201`）；输出保持原样、**不排序**。
- 输出：第 2 参数（一维字符串数组变量）或省略时写 `RESULTS`（`RESULTS:0` 起）；返回值 = 写入个数（受容量限制，`RESULTS` 默认长度 100）。
- 空前缀返回 0。
- 想确认某个宏是否定义，用 `ISDEFINED`（`Emuera.EM_readme.txt:30`，返回 1/0）更直接。

## 用法

### int ENUMMACROBEGINSWITH(str 前缀)
```erb
#DEFINE VAR_IS_NUM 1
#DEFINE VAR_IS_STRING 2
#DEFINE OTHER 3

PRINTFORML {ENUMMACROBEGINSWITH("VAR_")}     ;→ 2
PRINTFORML [{RESULTS:0}] [{RESULTS:1}]        ;两个宏名（顺序不保证）
```

### int ENUMMACROBEGINSWITH(str 前缀, ref str 输出数组)
```erb
#DIMS NAMES, 50
PRINTFORML {ENUMMACROBEGINSWITH("VAR_", NAMES)}   ;→ 写入个数
PRINTFORML {ISDEFINED(NAMES:0)}                   ;→ 1（该宏确实已定义）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:232`（`["ENUMMACROBEGINSWITH"] = new EnumNameMethod(EnumNameMethod.EType.Macro, EnumNameMethod.EAction.BeginsWith)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:151`（`EnumNameMethod`）
- 名字来源：`Runtime/Script/Data/IdentifierDictionary.cs:34`（`MacroKeys`）

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
            case BeginsWith:
                if item.ToUpper().IndexOf(arg, Ordinal) == 0: strs.Add(item)
    output = (args.Count == 2) ? args[1] 的字符串数组 : RESULTS 数组
    return Math.Min(output.Length, strs.Count)
```

## 备注

- 关于「何谓宏」：`#DEFINE` 定义的名字（`DefineMacro.Keyword`，`Runtime/Script/Data/DefineMacro.cs:5-18`）；与变量、函数名分属不同命名空间，故 `ENUMMACRO*` 不会枚举到变量或函数。
- 宏在 Emuera 中属于「编译期符号」：枚举出来之后不能再按名字动态取值（没有「求宏值」的函数），通常只用于诊断/自检类脚本（例如检查配置用的宏是否齐全）。
- 与 `ENUMFUNC*`、`ENUMVAR*` 同源实现，返回/输出约定完全一致。
