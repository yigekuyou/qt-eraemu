# VARSETEX

- **类别**：式中函数（EM 扩展 / 变量名字符串化操作）
- **签名**：`int VARSETEX(str 变量名, <值>{, int 全维赋值, int 起始索引, int 终止索引})`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:91-103`（「◆ int(1) VARSETEX str, value(, int, int, int)」）有记载。语义以源码为准。

## 语义

命令 `VARSET` 的字符串变量名版：第 1 参数不是变量名本身而是一个字符串表达式（内容为变量名，可带下标）。把指定范围内的元素批量赋为同一个值。

参数含义（与 readme 一致，源码逐条对应）：

- 第 2 参数：填充值。数值变量填整数，字符串变量填字符串（运行时按变量实际类型检查，不符抛 `CodeEE`）。
- 第 3 参数（全维赋值）：非 `0` 或**省略**时对该变量的所有维度都赋值；为 `0` 时只对「最低次元」赋值（即用第 1 参数里写明的下标定住前面的维度，只在最后一个维度上铺开）。
- 第 4、5 参数：起始索引与终止索引，作用于**最后一个维度**（与 `VARSET` 的半开区间 `[起始, 终止)` 一致）。省略第 5 参数时按变量维数取该维长度作为终止索引。

返回值恒为 `1`。读取/单点写入的对应函数分别是 `GETVAR`/`GETVARS` 与 `SETVAR`。

## 用法

### int VARSETEX(变量名, 值{, 全维赋值, 起始索引, 终止索引})
- 变量名：字符串表达式，内容须是变量（可带下标）；解析结果必须是单个变量项。
- 值：填充值，类型须与变量一致。
- 全维赋值：`0` → 只填最低次元；非 `0`/省略 → 全维填充。
- 起始索引 / 终止索引：最低次元的半开区间，终止索引须写满 5 个参数才生效。
- 返回值：恒 `1`。
```erb
; EM readme 的原始示例（readme 中 #DIM FOO,2,3 应为 #DIM FOO = 2,3）
#DIM FOO = 2,3

VARSETEX "FOO:1:1", 5, 0
PRINTFORML {FOO:1:0} {FOO:1:2}    ; 0 5
VARSETEX "FOO:1:0", 10
PRINTFORML {FOO:0:0} {FOO:1:2}    ; 10 10
```

```erb
; 用下标变量指定范围
#DIM A = 100
VARSETEX "A", 7, 1, 10, 20        ; A:10 ~ A:19 = 7
VARSETEX "A", 0                   ; 整个数组清零

; 字符串变量
#DIMS S = 10
VARSETEX "S", "空", 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:241`（`["VARSETEX"] = new VarSetExMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:541`（`VarSetExMethod`），批量写入落到 `Runtime/Script/Statements/Variable/VariableToken.cs:111`（`VariableToken.SetValueAll`）与各维数组的直接赋值

```text
VarSetExMethod:
    构造:
        ReturnType = long
        argumentTypeArrayEx = [ { ArgTypes = { String, Any, Int, Int, Int }, OmitStart = 2 } ]
            # 2~5 个参数：第 3 参数起可省略
        CanRestructure = false
    GetIntValue(exm, arguments):
        name = arguments[0] 的字符串值                      # 仅用于错误信息
        wc = LexicalAnalyzer.Analyse(new CharStream(arguments[0] 的字符串值), EoL, None)
        term = ExpressionParser.ReduceExpressionTerm(wc, TermEndWith.EoL)   # 运行时重新解析
        若 term 不是 VariableTerm → 抛 CodeEE(IsNotVar, name)
        var = term
        若 var.Identifier == null 或 var.Identifier.IsConst → 抛 CodeEE(IsNotVar, name)

        start = (参数个数 >= 4) ? arguments[3] 的整数值 : 0
        end   = (参数个数 == 5) ? arguments[4] 的整数值
                : (一维 ? GetLength()                     # 该维长度
                  : 二维 ? GetLength(1)                   # 第 2 维长度
                  : 三维 ? 0                              # ★ 源码重复写了 IsArray2D 判断，三维时取到 0
                  : 0)
        setAllDims = (参数个数 >= 3) ? (arguments[2] 的整数值 != 0) : true

        取 idx1/idx2/idx3 = var.GetElementInt(0/1/2)      # 变量名里写明的下标

        若 var.IsString:                                   # 字符串型
            val = ""（参数不足时）否则 arguments[1] 的字符串值
            若 arguments[1] 的类型 != string → 抛 CodeEE(SetStrToInt, name)  # 「文字列型でない変数"{0}"に文字列型を代入しようとしました」
            若 一维: Identifier.SetValueAll(val, start, end, 0)
            若 二维: for i = max(start, idx2) .. end-1: array[idx1, i] = val
            若 三维: for i = max(start, idx3) .. end-1: array[idx2, idx1, i] = val   # ★ 前两维下标互换
        否则:                                              # 整数型
            val = 0（参数不足时）否则 arguments[1] 的整数值
            若 arguments[1] 的类型 != long → 抛 CodeEE(SetIntToStr, name)
            若 一维: Identifier.SetValueAll(val, start, end, 0)                    # for i in [start,end): array[i] = val
            若 二维:
                若 setAllDims: for j in 0..GetLength(0)-1: for i = max(start, idx2) .. end-1: array[j, i] = val
                否则:         for i = max(start, idx2) .. end-1: array[idx1, i] = val
            若 三维:
                若 setAllDims: for k in 0..GetLength(0)-1: for j in 0..GetLength(1)-1:
                                   for i = max(start, idx3) .. end-1: array[k, j, i] = val
                否则:         for i = max(start, idx3) .. end-1: array[idx2, idx1, i] = val   # ★ 前两维下标互换
        返回 1
```

## 备注

- 语义据源码；readme 描述（「第 3 参数非 0 或省略时给数组全部赋值，否则给最低次元的数组赋值」）与源码的 `setAllDims` 分支一致，readme 的二维示例结果 `0 5` / `10 10` 经逐行推演与源码相符。
- **三维时的两个源码缺陷（据源码推得，未实际运行验证，标注为推定）**：
  1. `end` 的默认值计算把三维的判断写成了第二次 `IsArray2D`（`Runtime/Script/Statements/Function/Creator.Method.cs:566`），导致三维变量在省略第 5 参数时 `end = 0`，所有三维写入循环 `i < 0` 都不执行——即**对三维变量必须显式给出第 4、5 参数，否则整条 `VARSETEX` 静默什么也不做**，却仍返回 1。
  2. 三维的「只填最低次元」分支写的是 `array[idx2, idx1, i]`（`:592`、`:637`），而三维变量在 Emuera 中的下标顺序是 `array[第1下标, 第2下标, 第3下标]`（`Runtime/Script/Statements/Variable/VariableToken.cs:732`），且同一函数的 `setAllDims` 分支用的是 `array[k, j, i]`（`:629-632`）。两种顺序并存，说明非全维分支的前两维下标被写反了。
- 与命令 `VARSET` 的差异：`VARSET` 的多维情况会忽略第 3、4 参数而填满全部元素（见 `test/data/commands/VARSET.md`），`VARSETEX` 反而支持「只填最低次元」并支持范围；`VARSETEX` 不做 `IsCalc`（只读计算变量）检查，而 `VARSET` 的 `SetValueAll` 会跳过 `IsCalc`。
- 变量名里可以写变量作下标（如 `"A:IDX"`），下标在运行时求值；变量名解析结果必须是单个变量项，`"A + 1"` 之类会抛「が変数ではありません」。
- 参数个数下限是 2（`OmitStart = 2`），所以 `VARSETEX "A"` 会在解析期报参数不足；不存在「只给变量名把数组清零」的写法（对比 `VARSET FLAG`）。
