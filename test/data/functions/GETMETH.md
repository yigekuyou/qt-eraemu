# GETMETH

- **类别**：式中函数（daughter-patch 追加，`#region daughter-patch追加`）
- **签名**：int GETMETH(str 函数名{, int 未定义时的默认值{, 实参...}})
- **文档来源**：两套中文文档（`ecd/`、`_extracted/zh/`）与 EM/EE readme 均未收录，语义据源码

## 语义

按**名字字符串**动态调用用户定义表达式内函数（`#FUNCTION`），返回其整数值：

- 名字解析：`GetFunctionMethod(LabelDictionary, name, 实参表, userDefinedOnly: true)`，其中实参表 = 第 3 个及以后的参数（`arguments.Skip(2)`，`Runtime/Script/Statements/Function/Creator.Method.cs:7483`）。只认用户定义函数，事件函数与内置函数不可用。
- **未定义时的兜底**：若名字解析失败（函数不存在），则返回第 2 参数的值；若第 2 参数省略（或留空）→ 抛 `CodeEE`（`NotDefinedUserFunc`，`Runtime/Script/Statements/Function/Creator.Method.cs:7488-7491`）。
- 函数存在但返回**字符串**（`#FUNCTIONS`）→ 抛 `CodeEE`（`IsNotInt`）；实参个数/类型不匹配同样抛 `CodeEE`（由 `Create`/`ConvertArg` 抛出，不兜底）。
- 与 `CALLF` 等指令的差异：本函数在**表达式内**调用并直接得到返回值，且支持「函数不存在时取默认值」这一容错形态。
- 参数表：第 1 参（名字）与第 2 参（默认值）之后的实参一律按「任意类型」处理（`VariadicAny`），原样转交给被调用函数做类型匹配；被调用函数的参数默认值、引用参数等规则与直接调用一致。

## 用法

### int GETMETH(str 函数名{, int 默认值{, 实参...}})
- 函数名：字符串表达式（可含 `@`? 不——写的是**函数名**，即 `@` 后的部分）。
- 默认值：整数表达式；函数不存在时返回它。省略时「函数不存在」会报错。
- 实参：按被调用函数的参数表逐个给出。
- 返回值：被调用函数的整数值；不存在时返回默认值。
```erb
@CALC(a, b)
#FUNCTION
	RETURNF a * 10 + b

PRINTFORML {GETMETH("CALC", 0, 3, 4)}      ;→ 34（调用 CALC(3, 4)）
PRINTFORML {GETMETH("NOT_DEFINED", -1)}    ;→ -1（不存在 → 默认值）
; PRINTFORML {GETMETH("NOT_DEFINED")}      ; 省略默认值 → 运行期报错

@NAME_OF()
#FUNCTIONS
	RETURNF "甲"

; PRINTFORML {GETMETH("NAME_OF", 0)}      ; 报错：被调用函数返回字符串
PRINTFORML {GETMETHS("NAME_OF", "")}       ;→ 甲（字符串版）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:341`（`["GETMETH"] = new GetMethMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7468`（`GetMethMethod`）
- 名字解析：`Runtime/Script/Data/IdentifierDictionary.cs:580`（`GetFunctionMethod`）

```text
构造（Creator.Method.cs:7470-7478）:
    返回类型 = typeof(Int64)
    argumentTypeArrayEx = [{ String, Int, VariadicAny }, OmitStart = 1]
    ; 第 1 参（函数名）必填；第 2 参（默认值）与之后的实参可省/可变
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:7480-7497）:
    name = args[0].GetStrValue(exm)
    methArgs = args.Skip(2)                                ; 交给被调用函数的实参
    term = GlobalStatic.IdentifierDictionary.GetFunctionMethod(
               GlobalStatic.LabelDictionary, name, methArgs, true)
    if term == null:                                       ; 函数不存在
        if args.Count < 2 || args[1] == null:
            throw CodeEE(NotDefinedUserFunc, name)          ; 没有默认值 → 报错
        else:
            return args[1].GetIntValue(exm)                 ; 返回默认值
    elif !term.IsInteger:
        throw CodeEE(IsNotInt, name)                        ; 返回的是字符串
    else:
        return term.GetIntValue(exm)                        ; 真正调用该函数
```

## 备注

- 与 `GETMETHS`（字符串版）成对：本函数要求被调用函数是 `#FUNCTION`（数值型），否则报错。
- 名字解析用 `userDefinedOnly: true`：给内置函数名（如 `"RAND"`）会被当作「不存在」→ 返回默认值（而不是报「类型不符」），可作为特性使用（推定：源码路径即如此）。
- 「函数不存在」被区分成「有默认值 → 静默返回」与「无默认值 → 报错」两条路，这在脚本里常用于「可选回调」的写法：`GETMETH("OPTIONAL_HOOK", 0, ARG)`。
- 被调用函数若有引用型参数（`#DIM REF`），传入的也必须是变量——但经过字符串名字层无法表达引用意图（推定：按普通实参处理，引用参数会匹配失败报错）。
