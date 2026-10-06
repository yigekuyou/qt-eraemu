# ENUMFUNCENDSWITH

- **类别**：式中函数（EM 私家版扩展，名字枚举函数族）
- **签名**：int ENUMFUNCENDSWITH(str 后缀{, ref str 输出数组})
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:64-78`（组内「ENDFUNCENDSWITH」条目：`ENUMFUNCENDSWITH str`）；两套中文文档未收录；第 2 参数为文档未记载的源码扩展

## 语义

枚举**已定义的非事件标签名（函数名）**中所有以 `<后缀>` 结尾的名字，返回**实际写入目标数组的个数**。

- 枚举对象是 `Process.LabelDictionary.NoneventKeys`（普通 `@标签` 与 `#FUNCTION(S)` 函数名；事件函数不在内，`Runtime/Script/Data/LabelDictionary.cs:15, 71-81`）。
- 匹配**不区分大小写**（两侧 `ToUpper()` 后比较，`Runtime/Script/Statements/Function/Creator.Method.cs:179, 204`）；输出名字保持原样、**不排序**（源码 `strs.Sort()` 被注释，`:211`）。
- 后缀判定是「`LastIndexOf(后缀) == 长度 - 后缀长`」——等价于「以该后缀结尾」（`Runtime/Script/Statements/Function/Creator.Method.cs:204`）。
- 输出位置：第 2 参数（一维字符串数组变量）或省略时写 `RESULTS`（`RESULTS:0` 起）。
- 返回值 = 写入个数 `min(目标数组长度, 命中数)`；命中超过容量时被截断（`RESULTS` 默认长度 100）。
- 空字符串后缀返回 0（`arg.Length > 0` 守卫），而非「匹配全部」。
- 与 `ENUMFUNCBEGINSWITH` 只差比较方向；与 `ENUMFUNCWITH`（子串）是「后缀」与「包含」的区别。

## 用法

### int ENUMFUNCENDSWITH(str 后缀)
```erb
@CALC_DAMAGE
#FUNCTION
	RETURNF 1
@CALC_HIT
#FUNCTION
	RETURNF 1
@SHOW
#FUNCTION
	RETURNF 0

PRINTFORML {ENUMFUNCENDSWITH("_HIT")}     ;→ 1
PRINTFORML [{RESULTS:0}]                  ;→ [CALC_HIT]
```

### int ENUMFUNCENDSWITH(str 后缀, ref str 输出数组)
```erb
#DIMS NAMES, 50
PRINTFORML {ENUMFUNCENDSWITH("_HIT", NAMES)}   ;→ 写入个数
PRINTFORML [{NAMES:0}]
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:227`（`["ENUMFUNCENDSWITH"] = new EnumNameMethod(EnumNameMethod.EType.Function, EnumNameMethod.EAction.EndsWith)`）
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
            case EndsWith:
                if item.ToUpper().LastIndexOf(arg, Ordinal) == item.Length - arg.Length:
                    strs.Add(item)
    output = (args.Count == 2) ? args[1] 的字符串数组 : RESULTS 数组
    return Math.Min(output.Length, strs.Count)   ; 写入个数（Array.Copy 截断）
```

## 备注

- 与 readme 的「総数を返します」表述存在与 `ENUMFUNCBEGINSWITH` 相同的差异（返回写入数而非命中总数）。
- 后缀匹配按「最后一次出现的位置正好在末尾」实现，故模式里含通配符并不会被解释——它是纯字符串比较，不是通配匹配。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
