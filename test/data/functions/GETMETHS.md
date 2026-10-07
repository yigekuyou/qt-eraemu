# GETMETHS

- **类别**：式中函数（daughter-patch 追加，`#region daughter-patch追加`）
- **签名**：str GETMETHS(str 函数名{, str 未定义时的默认值{, 实参...}})
- **文档来源**：两套中文文档（`ecd/`、`_extracted/zh/`）与 EM/EE readme 均未收录，语义据源码

## 语义

`GETMETH` 的字符串版：按**名字字符串**动态调用用户定义表达式内函数（`#FUNCTIONS`），返回其字符串值。

- 名字解析：`GetFunctionMethod(LabelDictionary, name, 实参表, userDefinedOnly: true)`，实参表 = 第 3 个及以后的参数（`Runtime/Script/Statements/Function/Creator.Method.cs:7513`）。
- **未定义时的兜底**：函数不存在 → 返回第 2 参数的字符串值；第 2 参数省略或留空 → 抛 `CodeEE`（`NotDefinedUserFunc`）。
- 函数存在但返回**数值**（`#FUNCTION`）→ 抛 `CodeEE`（`IsNotStr`）；实参不匹配同样抛 `CodeEE`。
- 与 `GETMETH` 的差别只有返回类型要求（`#FUNCTIONS` vs `#FUNCTION`）与默认值类型（字符串 vs 整数）。

## 用法

### str GETMETHS(str 函数名{, str 默认值{, 实参...}})
- 函数名：字符串表达式（`@` 后的名字）。
- 默认值：字符串表达式；函数不存在时返回它。
- 实参：按被调用函数的参数表给出。
- 返回值：被调用函数的字符串值；不存在时返回默认值。
```erb
@PICK(a)
#FUNCTIONS
	SIF a == 1
		RETURNF "甲"
	RETURNF "乙"

PRINTFORML [{GETMETHS("PICK", "", 1)}]        ;→ [甲]
PRINTFORML [{GETMETHS("PICK", "", 2)}]        ;→ [乙]
PRINTFORML [{GETMETHS("NO_SUCH", "缺省")}]     ;→ [缺省]（不存在 → 默认值）
; PRINTFORML [{GETMETHS("NO_SUCH")}]           ; 省略默认值 → 运行期报错

@NUM_FN()
#FUNCTION
	RETURNF 1

; PRINTFORML [{GETMETHS("NUM_FN", "")}]        ; 报错：被调用函数返回数值
PRINTFORML {GETMETH("NUM_FN", 0)}              ;→ 1（数值版）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:342`（`["GETMETHS"] = new GetMethsMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7499`（`GetMethsMethod`）
- 名字解析：`Runtime/Script/Data/IdentifierDictionary.cs:580`（`GetFunctionMethod`）

```text
构造（Creator.Method.cs:7501-7509）:
    返回类型 = typeof(string)
    argumentTypeArrayEx = [{ String, String, VariadicAny }, OmitStart = 1]
    ; 第 1 参（函数名）必填；第 2 参（默认值，字符串）与之后的实参可省/可变
    CanRestructure = false

GetStrValue(exm, args)（Creator.Method.cs:7510-7527）:
    name = args[0].GetStrValue(exm)
    methArgs = args.Skip(2)
    term = GlobalStatic.IdentifierDictionary.GetFunctionMethod(
               GlobalStatic.LabelDictionary, name, methArgs, true)
    if term == null:
        if args.Count < 2 || args[1] == null:
            throw CodeEE(NotDefinedUserFunc, name)
        else:
            return args[1].GetStrValue(exm)          ; 默认值
    elif !term.IsString:
        throw CodeEE(IsNotStr, name)                 ; 返回的是数值
    else:
        return term.GetStrValue(exm)                 ; 真正调用
```

## 备注

- readme 与中文文档均无记载；与 `GETMETH` 同区（daughter-patch），二者是同一实现思路的类型双胞胎。
- 「默认值只在函数不存在时生效」；函数存在但签名不匹配（实参个数/类型错）会直接报错，**不会**退回默认值（源码分支结构使然）。
- 被调用函数的返回值也会做类型匹配：`#FUNCTIONS` 用本函数、`#FUNCTION` 用 `GETMETH`，写反会报 `IsNotStr`/`IsNotInt`。
