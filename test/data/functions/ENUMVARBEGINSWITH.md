# ENUMVARBEGINSWITH

- **类别**：式中函数（EM 私家版扩展，名字枚举函数族）
- **签名**：int ENUMVARBEGINSWITH(str 前缀{, ref str 输出数组})
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:64-78`（组内「ENUMVARBEGINSWITH str」）；两套中文文档未收录；第 2 参数为文档未记载的源码扩展

## 语义

枚举**已定义变量名**中所有以 `<前缀>` 开头的名字，返回**实际写入目标数组的个数**。

- 枚举对象是 `GlobalStatic.IdentifierDictionary.VarKeys`（`Runtime/Script/Data/IdentifierDictionary.cs:33` → `varTokenDic.Keys`），其中既有**系统变量**（`RESULT`、`RESULTS`、`TARGET`、`DAY`、`MONEY`、`FLAG`… 见 `Runtime/Script/Statements/Variable/VariableData.cs:149-208`）也有**用户定义的广域变量**（写在 ERH 里的 `#DIM`/`#DIMS` 等，经 `AddUseDefinedVariable` 加入，`Runtime/Script/Data/IdentifierDictionary.cs:431-433` 与 `Runtime/Script/Loader/ErhLoader.cs:290`）；写在 ERB 函数内的私有 `#DIM` 不在其中。
- 只枚举名字，**不区分数值/字符串、维度**；想查某个具体变量的性质用 `EXISTVAR`。
- 匹配**不区分大小写**（两侧 `ToUpper()`），输出保持字典中的原样、**不排序**（`strs.Sort()` 被注释，`Runtime/Script/Statements/Function/Creator.Method.cs:211`）。
- 输出：第 2 参数（一维字符串数组变量）或省略时写 `RESULTS`（`RESULTS:0` 起）；返回值 = 写入个数（受容量限制，`RESULTS` 默认长度 100）。
- 空前缀返回 0。

## 用法

### int ENUMVARBEGINSWITH(str 前缀)
```erb
; 这些变量需定义在 ERH（头文件）里，才是能被枚举到的广域变量
#DIM MORALE = 0
#DIMS MORALE_NOTE = ""
#DIM HP = 100

PRINTFORML {ENUMVARBEGINSWITH("MORALE")}     ;→ 2
FOR i, 0, RESULT
	PRINTFORML {RESULTS:i}                   ; MORALE 与 MORALE_NOTE（顺序不保证）
NEXT
```

### int ENUMVARBEGINSWITH(str 前缀, ref str 输出数组)
```erb
#DIMS NAMES, 50
PRINTFORML {ENUMVARBEGINSWITH("MORALE", NAMES)}   ;→ 2（容量足够时即为命中数）
PRINTFORML {EXISTVAR(NAMES:0)}                    ;→ 变量性质位（见 EXISTVAR）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:229`（`["ENUMVARBEGINSWITH"] = new EnumNameMethod(EnumNameMethod.EType.Variable, EnumNameMethod.EAction.BeginsWith)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:151`（`EnumNameMethod`）
- 名字来源：`Runtime/Script/Data/IdentifierDictionary.cs:33`（`VarKeys => varTokenDic.Keys.ToArray()`）、用户变量注册于 `:431-433`

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
            case BeginsWith:
                if item.ToUpper().IndexOf(arg, Ordinal) == 0: strs.Add(item)
    output = (args.Count == 2) ? args[1] 的字符串数组 : RESULTS 数组
    return Math.Min(output.Length, strs.Count)
```

## 备注

- 返回的名字是**变量名而非值**；取得值请用 `GETVAR`/`GETVARS`（按变量名表达式求值）。
- 因为包含系统变量，前缀如 `"RESULT"` 会命中 `RESULT`、`RESULTS`（以及玩家自定义的同前缀变量）——按名字批量操作变量时要意识到系统变量也在集合里。
- 与 `ENUMFUNC*`（函数名）、`ENUMMACRO*`（`#DEFINE` 宏名）同源实现。
