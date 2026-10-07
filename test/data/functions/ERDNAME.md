# ERDNAME

- **类别**：式中函数（EE 扩展，`#region EEで追加されたやつ`）
- **签名**：str ERDNAME(var 变量{, int 数值{, int 维度}})
- **文档来源**：两套中文文档（`ecd/`、`_extracted/zh/`）与 EM/EE readme 均未收录本函数名；ERD 机制本身见于 `emuera.em/Readme/EmueraEE_readme.txt:124-126`「・ERHで定義した変数にcsvファイルで名前を付けられるように」（ERB 目录下的「变量名.ERD」文件），本函数的语义据源码

## 语义

`ERD`（ERB 目录下「变量名.ERD」文本定义文件，给 `#DIM` 变量名批量绑定的名字表）的**反查**函数：给出变量与数值，返回该数值对应的**名字（关键字）**；查不到返回空字符串。

- 第 1 参数必须是**变量引用**（`ArgType.RefAny | AllowConstRef`，常量变量也允许），源码直接把它当作 `VariableTerm` 取变量名（`Runtime/Script/Statements/Function/Creator.Method.cs:7361`）。
- 第 2 参数：要反查的数值；**负数直接查不到**（`TryIntegerToKeyword` 里 `value < 0 → false`，`Runtime/Script/Data/ConstantData.cs:861`）。
- 第 3 参数（可省，仅 2 参时省略）：维度序号，用于多维变量的 ERD 表——查表键变成 `"变量名@N"`（`Runtime/Script/Statements/Function/Creator.Method.cs:7364`）。ERD 文件按规则登记为 `变量名`（一维）或 `变量名@1`/`@2`/`@3`（二、三维的每个维度各一份，`Runtime/Script/Loader/ErhLoader.cs:295-330`）。
- 命中返回该名字；变量名未登记 ERD、值不在表中、值为负 → 返回 `""`。
- 同名不同值的匹配来自一个 `Dictionary<string,int>` 的反向扫描（`FirstOrDefault(x => x.Value == value)`）：**若多个名字对应同一数值，返回哪一个取决于字典的枚举顺序**（不保证），这是实现细节（推定）。
- 本函数 `CanRestructure = true` 且 `HasUniqueRestructure = true`：当第 2 参数在重构后可折叠为常量（`SingleTerm`）时，整个调用会在**解析期**折叠成常量字符串（`Runtime/Script/Statements/Function/Creator.Method.cs:7373-7377`），即 ERD 反查在装载时完成。

## 用法

### str ERDNAME(var 变量, int 数值{, int 维度})
- 变量：必须是变量（如 `FOO`、`FOO:1`），不能是表达式或字面量；常量变量也允许。
- 数值：整数表达式。
- 维度：可选，多维变量的维度号（1 起）。
- 返回值：名字字符串；查不到 `""`。
```erb
; 前提：ERB 目录下有 FOO.ERD（内容形如每行一个名字，与 CSV 变量文件同格式）
#DIM FOO, 3
PRINTFORML [{ERDNAME(FOO, 0)}]     ;→ FOO 中数值 0 对应的名字
PRINTFORML [{ERDNAME(FOO, 1)}]     ;→ 数值 1 对应的名字
PRINTFORML [{ERDNAME(FOO, 999)}]   ;→ []（未登记该值）
PRINTFORML [{ERDNAME(FOO, -1)}]    ;→ []（负数一律查不到）

; 二维变量的某一维
#DIM BAR, 3, 3
PRINTFORML [{ERDNAME(BAR, 2, 1)}]  ; 查 "BAR@1" 表
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:329`（`["ERDNAME"] = new ErdNameMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7347`（`ErdNameMethod`）
- 反查表：`Runtime/Script/Data/ConstantData.cs:858`（`TryIntegerToKeyword`）、表字段与装载 `:112`（`erdNameToIntDics`）、`:721`（`UserDefineLoadData`）
- ERD 文件登记：`Runtime/Script/Loader/ErhLoader.cs:295-330`（一维用变量名，多维用 `变量名@1..@3`）

```text
构造（Creator.Method.cs:7349-7358）:
    返回类型 = typeof(string)
    argumentTypeArrayEx = [{ RefAny | AllowConstRef, Int, Int }, OmitStart = 2]
    ; 第 1 参必须是变量引用；第 2 参必填；第 3 参（维度）可省
    CanRestructure = true ; HasUniqueRestructure = true

GetStrValue(exm, args)（Creator.Method.cs:7359-7372）:
    vToken = (VariableTerm)args[0]
    varname = (args.Count > 2) ? vToken.Identifier.Name + "@" + args[2] : vToken.Identifier.Name
    value = args[1].GetIntValue(exm)
    if exm.VEvaluator.Constant.TryIntegerToKeyword(out ret, value, varname): return ret
    return ""

TryIntegerToKeyword（ConstantData.cs:858-874）:
    ret = ""; if value < 0: return false
    if varname 为空: return false
    if !erdNameToIntDics.TryGetValue(varname, out dic): return false
    kvp = dic.FirstOrDefault(x => x.Value == value)      ; 反向扫描，重复值取枚举顺序首个
    if kvp.Key 为空: return false
    ret = kvp.Key; return true

UniqueRestructure（Creator.Method.cs:7373-7377）:
    args[1] = args[1].Restructure(exm)
    return (args[1] is SingleTerm)      ; 常量 → 允许把整个调用折叠为常量
```

## 备注

- 与同名机制的前向函数对照：`ERD` 让「名字 → 数值」在解析期生效（写在 ERB 里直接用名字当常量），`ERDNAME` 是唯一的**反查**入口（数值 → 名字），以便把数值显示回名字。
- 第 1 参数传「非变量」时会在参数检查期报错（`RefAny` 要求变量）；传「未登记 ERD 的变量名」不报错，只是返回空串。
- 第 3 参数只影响查表键的拼法（`名字@N`），不校验维度号范围；给 3 维变量传 `1`..`3`，给 1 维变量传第 3 参会查不到（因为一维的表键没有 `@N` 后缀）。
- ERD 功能受配置开关 `Config.Config.UseERD` 控制（`Runtime/Script/Loader/ErhLoader.cs:45`、`Runtime/Script/Data/ConstantData.cs:1101-1102`）；开关关闭时没有 ERD 表，本函数一律返回空串（推定）。
