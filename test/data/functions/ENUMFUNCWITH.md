# ENUMFUNCWITH

- **类别**：式中函数（EM 私家版扩展，名字枚举函数族）
- **签名**：int ENUMFUNCWITH(str 子串{, ref str 输出数组})
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:64-78`（组内「WITH」条目：`ENUMFUNCWITH str`）；两套中文文档未收录；第 2 参数为文档未记载的源码扩展

## 语义

枚举**已定义的非事件标签名（函数名）**中所有**包含** `<子串>` 的名字，返回**实际写入目标数组的个数**。

- 包含判定是 `IndexOf(子串) >= 0`（`Runtime/Script/Statements/Function/Creator.Method.cs:207`），即子串出现在任意位置即可（前缀/后缀/中间都算）。
- 枚举对象 `Process.LabelDictionary.NoneventKeys`（普通 `@标签`、`#FUNCTION(S)`；事件函数除外）。
- 匹配**不区分大小写**（两侧 `ToUpper()`）；输出名字保持原样且**不排序**。
- 输出：第 2 参数（一维字符串数组变量）或省略时写 `RESULTS`（`RESULTS:0` 起）。
- 返回值 = 写入个数（`min(目标容量, 命中数)`）；命中超出容量被截断。
- 空字符串子串返回 0（源码守卫 `arg.Length > 0`，`:194`）——注意这在数学上是「空串是任何串的子串」，但实现选择不匹配。

## 用法

### int ENUMFUNCWITH(str 子串)
```erb
@BATTLE_START
#FUNCTION
	RETURNF 1
@BATTLE_END
#FUNCTION
	RETURNF 1
@DAILY
#FUNCTION
	RETURNF 0

PRINTFORML {ENUMFUNCWITH("BATTLE")}     ;→ 2
PRINTFORML [{RESULTS:0}] [{RESULTS:1}]   ;两行结果（顺序不保证）
```

### int ENUMFUNCWITH(str 子串, ref str 输出数组)
```erb
#DIMS NAMES, 50
PRINTFORML {ENUMFUNCWITH("_", NAMES)}   ;→ 写入个数（含下划线的函数名）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:228`（`["ENUMFUNCWITH"] = new EnumNameMethod(EnumNameMethod.EType.Function, EnumNameMethod.EAction.With)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:151`（`EnumNameMethod`）

```text
构造（Creator.Method.cs:167-176）:
    返回类型 = long
    argumentTypeArrayEx = [{ String, RefString1D }, OmitStart = 1]
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:177-221）:
    arg = args[0].GetStrValue(exm).ToUpper()
    array = GlobalStatic.Process.LabelDictionary.NoneventKeys        ; Function 类
    strs = []
    if arg.Length > 0:
        foreach item in array:
            if item.Length < arg.Length: continue
            case With:
                if item.ToUpper().IndexOf(arg, Ordinal) >= 0: strs.Add(item)
    output = (args.Count == 2) ? args[1] 的字符串数组 : RESULTS 数组
    return Math.Min(output.Length, strs.Count)   ; 写入个数
```

## 备注

- 三个 FUNC 版（`ENUMFUNC*`）只差比较方式：`BEGINSWITH` 前缀 / `ENDSWITH` 后缀 / `WITH` 包含。
- 「命中数 > 100（`RESULTS` 默认容量）」时只写前 100 个、返回值也是 100；EM readme 的「総数を返します」在此情形不成立（同族共同差异）。
