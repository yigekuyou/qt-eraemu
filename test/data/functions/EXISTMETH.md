# EXISTMETH

- **类别**：式中函数（daughter-patch 追加，`#region daughter-patch追加`）
- **签名**：int EXISTMETH(str 函数名)
- **文档来源**：两套中文文档（`ecd/`、`_extracted/zh/`）与 EM/EE readme 均未收录，语义据源码

## 语义

判断**用户定义表达式内函数**（`#FUNCTION` / `#FUNCTIONS` 声明的 `@函数名`）是否存在，并返回其返回类型编码：

| 返回值 | 含义 |
|---|---|
| `1` | 存在，且是 `#FUNCTION`（数值型） |
| `2` | 存在，且是 `#FUNCTIONS`（字符串型） |
| `0` | 不存在，或**无法以零参数调用** |

- 判定方式是「以**空参数表**去解析该函数」：`GetFunctionMethod(..., name, new List<AExpression>(), userDefinedOnly: true)`（`Runtime/Script/Statements/Function/Creator.Method.cs:7544`）。
  - 因此只有「可以不带参数调用」的函数才会返回非 0：需要实参的函数在参数匹配阶段失败并抛 `CodeEE`，被本函数 `catch` 后返回 0（`Runtime/Script/Statements/Function/Creator.Method.cs:7542-7549`）；声明了默认值的参数不受影响（`ConvertArg` 会用默认值补齐，`Runtime/Script/Process.CalledFunction.cs:178-186`）。
  - `userDefinedOnly = true`：只认用户定义函数，内置式中函数（如 `RAND`）不在此列，事件函数也不算。
- 返回值是「类型编码」（1/2），不是布尔；判定存在应写 `EXISTMETH("FOO") != 0`。
- 与 `EXISTFUNCTION`（EE 版，`Runtime/Script/Statements/Function/Creator.cs:314`）不同：后者按名字判存否（源码未在本树细查，不属本批条目）；本函数的特色是「按表达式内函数的可调用性 + 返回类型」判定。
- `CanRestructure = true`：参数为常量时可折叠为常量（装载期判定）。

## 用法

### int EXISTMETH(str 函数名)
- 函数名：字符串表达式（大小写规则与 EraBasic 标识符一致，由字典比较器决定）。
- 返回值：`1` 数值型 / `2` 字符串型 / `0` 不存在或不可零参调用。
```erb
@GET_ZERO
#FUNCTION
	RETURNF 0

@GET_ONE(ARG)
#FUNCTION
	RETURNF ARG

PRINTFORML {EXISTMETH("GET_ZERO")}   ;→ 1（#FUNCTION，可零参调用）
PRINTFORML {EXISTMETH("GET_ONE")}    ;→ 0（需要实参，零参解析失败 → 0）
PRINTFORML {EXISTMETH("NOPE")}       ;→ 0

@GET_NAME
#FUNCTIONS
	RETURNF "x"

PRINTFORML {EXISTMETH("GET_NAME")}   ;→ 2（#FUNCTIONS → 字符串型）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:343`（`["EXISTMETH"] = new ExistMethMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7529`（`ExistMethMethod`）
- 解析入口：`Runtime/Script/Data/IdentifierDictionary.cs:580`（`GetFunctionMethod`）；参数匹配 `Runtime/Script/Process.CalledFunction.cs:154`（`ConvertArg`）

```text
构造（Creator.Method.cs:7531-7536）:
    返回类型 = typeof(Int64)
    argumentTypeArray = [typeof(string)]      ; 恰好 1 个字符串参数
    CanRestructure = true

GetIntValue(exm, args)（Creator.Method.cs:7538-7562）:
    name = args[0] 的字符串值
    try:
        term = GlobalStatic.IdentifierDictionary.GetFunctionMethod(
                   GlobalStatic.LabelDictionary, name, new List<AExpression>(), true)
                   ; 空参数表；userDefinedOnly = true
    catch (CodeEE): return 0                  ; 标识符不存在 / 参数不匹配 / 非 #FUNCTION 标签 …
    if term == null: return 0
    res = 0
    if term.IsInteger: res |= 1               ; typeof(long) → 1
    if term.IsString:  res |= 2               ; typeof(string) → 2
    return res

GetFunctionMethod 内部（IdentifierDictionary.cs:580-628，与本函数相关的分支）:
    若 labelDic 已初始化:
        若名字是 #FUNCTION/#FUNCTIONS 标签:
            若 userDefinedOnly 且 !IsMethod → throw CodeEE（不是函数）
            若 IsMethod → UserDefinedMethodTerm.Create(func, 空参数表, out errMes)
                失败（参数不匹配等）→ throw CodeEE(errMes) → 被上层 catch → 0
                （从而"需要实参的函数"返回 0）
    否则 → return null（内置函数在 userDefinedOnly 下不可见）
```

## 备注

- 与 `GETMETH`/`GETMETHS`（同区追加）配套：先用 `EXISTMETH` 判断再 `GETMETH` 调用；不过 `GETMETH` 自带「默认值」参数，能直接处理「未定义」的情况，因此实际脚本里 `EXISTMETH` 更多用于诊断或分支判断。
- 关键陷阱：**带必填参数的函数返回 0**（本函数用空参数表试探）。若想判「是否存在」而非「可否零参调用」，本函数并不适用（源码语义如此，且带默认值的参数仍算可零参）。
- 返回值 1/2 的编码与 `EXISTVAR`（1/2/4/8/16）风格一致：都是位/类型编码而非真假值。
- 本仓库移植版（`src/eraengine/`）未实现本函数。
