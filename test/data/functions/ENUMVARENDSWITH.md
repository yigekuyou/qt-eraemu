# ENUMVARENDSWITH

- **类别**：式中函数（EM 私家版扩展，名字枚举函数族）
- **签名**：int ENUMVARENDSWITH(str 后缀{, ref str 输出数组})
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:64-78`（组内「ENUMVARENDSWITH str」）；两套中文文档未收录；第 2 参数为文档未记载的源码扩展

## 语义

枚举**已定义变量名**中所有以 `<后缀>` 结尾的名字，返回**实际写入目标数组的个数**。

- 枚举对象 `GlobalStatic.IdentifierDictionary.VarKeys`（系统变量 + 用户定义的广域变量名；`Runtime/Script/Data/IdentifierDictionary.cs:33`（`VarKeys`）与 `:431-433`/`Runtime/Script/Loader/ErhLoader.cs:290`（ERH 变量注册）；ERB 函数内的私有 `#DIM` 不在其中）。
- 后缀判定：`LastIndexOf(后缀) == 长度 - 后缀长`（`Runtime/Script/Statements/Function/Creator.Method.cs:204`）。
- 匹配**不区分大小写**（两侧 `ToUpper()`）；输出名字保持原样、**不排序**。
- 输出：第 2 参数（一维字符串数组变量）或省略时写 `RESULTS`（`RESULTS:0` 起）；返回值 = 写入个数（受容量限制，`RESULTS` 默认长度 100）。
- 空后缀返回 0（源码守卫，不是「匹配全部」）。

## 用法

### int ENUMVARENDSWITH(str 后缀)
```erb
; 这些变量需定义在 ERH（头文件）里，才是能被枚举到的广域变量
#DIM SLOT_1 = 0
#DIM SLOT_2 = 0
#DIM HP = 100

PRINTFORML {ENUMVARENDSWITH("_1")}     ;→ 1
PRINTFORML [{RESULTS:0}]                ;→ [SLOT_1]
```
### int ENUMVARENDSWITH(str 后缀, ref str 输出数组)
```erb
#DIMS NAMES, 50
PRINTFORML {ENUMVARENDSWITH("_1", NAMES)}      ;→ 写入个数
PRINTFORML {GETVAR(NAMES:0)}                   ;→ 该变量的整数值（见 GETVAR）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:230`（`["ENUMVARENDSWITH"] = new EnumNameMethod(EnumNameMethod.EType.Variable, EnumNameMethod.EAction.EndsWith)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:151`（`EnumNameMethod`）

```text
构造（Creator.Method.cs:167-176）:
    返回类型 = long
    argumentTypeArrayEx = [{ String, RefString1D }, OmitStart = 1]
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:177-221）:
    arg = args[0].GetStrValue(exm).ToUpper()
    array = GlobalStatic.IdentifierDictionary.VarKeys        ; Variable 类
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

- 与 `ENUMVARBEGINSWITH` 只差比较方向；「枚举某前缀的变量后逐个 `GETVAR`/`VARSETEX` 批量处理」是这类函数的典型用法（`VARSETEX` 见 `Emuera.EM_readme.txt:91`）。
- 结果顺序不保证（`strs.Sort()` 被注释），若需要确定性顺序请自行排序。
