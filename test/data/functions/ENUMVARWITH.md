# ENUMVARWITH

- **类别**：式中函数（EM 私家版扩展，名字枚举函数族）
- **签名**：int ENUMVARWITH(str 子串{, ref str 输出数组})
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:64-78`（组内「ENUMVARWITH str」，注记「WITHはstrを含むシンボルです」）；两套中文文档未收录；第 2 参数为文档未记载的源码扩展

## 语义

枚举**已定义变量名**中所有**包含** `<子串>` 的名字，返回**实际写入目标数组的个数**。

- 包含判定 `IndexOf(子串) >= 0`（`Runtime/Script/Statements/Function/Creator.Method.cs:207`）：前缀、后缀、中间命中都算。
- 枚举对象是 `GlobalStatic.IdentifierDictionary.VarKeys`（`Runtime/Script/Data/IdentifierDictionary.cs:33` → `varTokenDic.Keys`），其中既有系统变量（`RESULT`/`RESULTS`/`DAY`/`MONEY`/`FLAG`… 见 `Runtime/Script/Statements/Variable/VariableData.cs:149-208`）也有用户定义的广域变量（写在 ERH 里的 `#DIM`/`#DIMS`，`Runtime/Script/Data/IdentifierDictionary.cs:431-433`/`Runtime/Script/Loader/ErhLoader.cs:290`）；ERB 函数内的私有 `#DIM` 不在其中。
- 匹配**不区分大小写**（两侧 `ToUpper()`）；输出名字保持字典中的原样、**不排序**（源码 `strs.Sort()` 被注释，`:211`）。
- 输出：第 2 参数（一维字符串数组变量）或省略时写 `RESULTS`（`RESULTS:0` 起）；返回值 = 写入个数，命中超出容量被截断（`RESULTS` 默认长度 100）。
- 空子串返回 0（源码 `if (arg.Length > 0)` 守卫）。

## 用法

### int ENUMVARWITH(str 子串)
```erb
; 这些变量需定义在 ERH（头文件）里，才是能被枚举到的广域变量
#DIM SLOT_A = 0
#DIM SLOT_B = 0
#DIM HP = 100

PRINTFORML {ENUMVARWITH("SLOT_")}        ;→ 2
FOR i, 0, RESULT
	PRINTFORML {RESULTS:i}                ; SLOT_A / SLOT_B（顺序不保证）
NEXT
PRINTFORML {ENUMVARWITH("LOT")}          ;→ 2（子串在中间也命中）
```

### int ENUMVARWITH(str 子串, ref str 输出数组)
```erb
#DIMS NAMES, 50
PRINTFORML {ENUMVARWITH("SLOT", NAMES)}   ;→ 2（容量足够时即为命中数）
PRINTFORML {GETVAR(NAMES:0)}              ;→ 该变量当前值（见 GETVAR）
VARSETEX NAMES:0, 1                       ; 按名字赋值（VARSETEX 支持字符串变量名）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:231`（`["ENUMVARWITH"] = new EnumNameMethod(EnumNameMethod.EType.Variable, EnumNameMethod.EAction.With)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:151`（`EnumNameMethod`）
- 名字来源：`Runtime/Script/Data/IdentifierDictionary.cs:33`（`VarKeys`）

```text
构造（Creator.Method.cs:167-176）:
    返回类型 = long
    argumentTypeArrayEx = [{ String, RefString1D }, OmitStart = 1]
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:177-221）:
    arg = args[0].GetStrValue(exm).ToUpper()
    array = GlobalStatic.IdentifierDictionary.VarKeys      ; Variable 类
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

- 三个 VAR 版（`ENUMVARBEGINSWITH` / `ENUMVARENDSWITH` / `ENUMVARWITH`）只差比较方式；实测时注意结果里会包含**系统变量**（例如子串 `"RESULT"` 会命中 `RESULT` 和 `RESULTS`）。
- 典型组合拳：`ENUMVARWITH` 找到一族变量名 → `GETVAR`/`GETVARS` 逐个读、`SETVAR`/`VARSETEX` 批量写（后两者的名字参数同样是字符串，见 `Emuera.EM_readme.txt:79-104`）。
- 返回值受目标容量限制（`RESULTS` 默认 100），命中很多时建议显式传大数组。
