# ARRAYMSORTEX

- **类别**：式中函数（EM 私家版扩展）
- **签名**：int ARRAYMSORTEX(str 排序基准变量名, str数组 跟随数组名表{, int 正逆{, int 固定长度}})
- **签名**：int ARRAYMSORTEX(var 排序基准数组, str数组 跟随数组名表{, int 正逆{, int 固定长度}})
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:105-133`「◆ int(1) ARRAYMSORTEX str/intArray, strArray(, int)」（含完整示例与结果）；两套中文文档（`ecd/`、`_extracted/zh/`）未收录

## 语义

`ARRAYMSORT` 的「以字符串指定变量名」版本：以第 1 参数为排序基准求出置换表，把第 2 参数字符串数组里**逐个列出的数组名**所指的数组按同一置换表整体重排。成功返回 `1`，失败/中止返回 `0`。

- 第 1 参数两种写法（由实参类型决定分支）：
  - **字符串**：变量名（可带下标，如 `"IDX"`）。名字在运行期用词法/语法分析器解析成变量引用（`GetConvertedTerm`，`Runtime/Script/Statements/Function/Creator.Method.cs:371-379`）；
  - **整数一维数组变量**：直接取该数组的值作为排序键（如 `ARRAYMSORTEX IDX, VARS`）。
- 第 2 参数：**一维字符串数组**，每个元素是「要跟随重排的数组」的变量名（可带下标）。注意：**基准数组自身若不在这个表里就不会被重排**（readme 示例注释「IDXを入れないとIDXを並び替えしない」）。
- 第 3 参数（可省）：0 以外或省略 → 升序；`0` → **降序**（`Runtime/Script/Statements/Function/Creator.Method.cs:382`）。
- 第 4 参数（可省）：固定长度。
  - 省略 → `-1`：整数基准数组读到第一个 `0` 即停止收录；字符串基准数组遇到空串则**直接返回 0**（中止整个调用，不是「停止收录」——两个分支行为不一致，见伪代码）。
  - 给定 `n` → 只处理前 `min(n, 数组长度)` 个元素；`n == 0` 时立即返回 `0`。
- 字符串比较用 .NET `string.CompareTo`（区域性比较），整数比较用 `Math.Sign(a.Key - b.Key)`（`Runtime/Script/Statements/Function/Creator.Method.cs:401, 415`）。
- 对被重排的每个数组：源码复制副本后按置换表回写，因此基准数组与跟随数组**可以重名**（含基准数组自身）。数组长度不足置换表长度 → 返回 `0`（不做部分重排）。
- 变量名解析失败、是非数组变量、是角色变量、是常量/计算变量时抛 `CodeEE`（`CheckVariableTerm`，`Runtime/Script/Statements/Function/Creator.Method.cs:358-370`）。

## 用法

### int ARRAYMSORTEX(str 排序基准变量名, str数组 跟随数组名表{, int 正逆{, int 固定长度}})
```erb
#DIM IDX = 4, 2, 3, 1
#DIM AA = 1, 2, 3, 4
#DIM BB = 5, 3, 1, 2
#DIMS VARS = "IDX", "AA", "BB"    ; 不含 "IDX" 则不重排 IDX 自身

ARRAYMSORTEX "IDX", VARS          ; 以 IDX 升序重排 IDX/AA/BB（readme 示例）
PRINTFORML > AA == {AA},{AA:1},{AA:2},{AA:3}    ;→ 4,2,3,1

ARRAYMSORTEX "IDX", VARS, 0       ; 第 3 参数为 0 → 降序
```

### int ARRAYMSORTEX(var 排序基准数组, str数组 跟随数组名表{, …})（第 1 参数直接给数组变量）
```erb
#DIM KEY = 4, 1, 3
#DIM DATA = 40, 10, 30
#DIMS TARGET = "DATA"

PRINTFORML {ARRAYMSORTEX(KEY, TARGET)}   ;→ 1；DATA 按 KEY 的升序重排为 10,30,40
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:242`（`["ARRAYMSORTEX"] = new ArrayMultiSortExMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:347`（`ArrayMultiSortExMethod`；`CheckVariableTerm` `:358`、`GetConvertedTerm` `:371`、`GetIntValue` `:380`）
- 对照实现：`ARRAYMSORT` 为 `ArrayMultiSortMethod`（`Runtime/Script/Statements/Function/Creator.Method.cs:4068` 附近），参数为变量本身而非名字

```text
构造（Creator.Method.cs:350-357）:
    返回类型 = long
    argumentTypeArrayEx = [
        { String,          RefString1D, Int, Int|DisallowVoid }, OmitStart = 2
        { RefInt1D,        RefString1D, Int, Int|DisallowVoid }, OmitStart = 2
    ]
    ; 第 1 参：字符串 或 整数一维数组；第 2 参：一维字符串数组（必填）
    ; 第 3（正逆）、第 4（长度）可省
    CanRestructure = false   ; 注意：虽重写了 UniqueRestructure，但未置 HasUniqueRestructure，
                             ; 故实际走基类通用重构（FunctionMethodTerm.cs:33-47）

GetIntValue(exm, args)（Creator.Method.cs:380-495）:
    isAscending = (args.Count < 3 || args[2] == null || args[2] != 0)
    fixedLength = (args.Count < 4) ? -1 : args[3]
    if fixedLength == 0: return 0
    varTerm = args[0] 若已是 VariableTerm 则直接用，否则 GetConvertedTerm(args[0] 的字符串值)
              ; 解析成变量词；非变量/角色变量/常量 → throw CodeEE
    if 基准数组是整数数组:
        length = fixedLength > 0 ? min(fixedLength, array.Length) : array.Length
        for i in 0..length-1:
            if fixedLength == -1 && array[i] == 0: break      ; 0 = 结束标志
            if array[i] < long.MinValue || array[i] > long.MaxValue: return 0   ; long 下恒假（遗留代码）
            sortList.Add((array[i], i))
        sortList 按 Key 升/降序排序（Math.Sign(a-b)，可能溢出）
    else（字符串数组）:
        length = fixedLength > 0 ? min(fixedLength, array.Length) : array.Length
        for i in 0..length-1:
            if fixedLength == -1 && 空串/Null: return 0        ; ← 与整数分支不同：中止而非 break
            sortList.Add((array[i], i))
        sortList 按 CompareTo 升/降序排序
    sortedArray = sortList 的原下标序列
    varTerms = 对 (args[1] 的字符串数组) 的每个元素执行 GetConvertedTerm
    for term in varTerms:
        if 一维数组:   clone ← 副本；若 array.Length < sortedArray.Length: return 0
                       for i: array[i] = clone[sortedArray[i]]
        elif 二维数组: 同上，按 array[i, x] = clone[sortedArray[i], x] 整行搬运
        elif 三维数组: 同上，按 [i, x, y] 搬运
        else: throw ExeEE（异常数组）
    return 1
```

## 备注

- readme 的签名写作 `int(1) ARRAYMSORTEX str/intArray, strArray(, int)`：只写了 1 个可选参数，且把「第 3 参数（正逆）」在正文里误称为「第 2 引数」（`Emuera.EM_readme.txt:108`：「第2引数は0以外または省略した場合正順、0の場合逆順」；从示例 `ARRAYMSORTEX "IDX", VARS, 0 ;逆順` 可确认它指的是第 3 个实参）。**第 4 参数「固定长度」readme 完全未记**，仅源码可见。
- 与 `ARRAYMSORT` 的差异：`ARRAYMSORTEX` 用**名字字符串**间接指定数组（因此可以在不知道变量名写法的运行期批量重排），且多了「固定长度」与「降序」参数；`ARRAYMSORT` 则以变量本身为参数、只有一个基准数组 + 若干跟随数组、恒定升序。注意 `ARRAYMSORT` 的基准数组中 0 是「结束标志」（跳过后续），EX 版整数分支沿用，但字符串分支改成直接失败。
- 「第 2 参数的每个元素都必须能解析为已定义的一维/多维数组变量」：空元素（无用的 `""`）会在 `GetConvertedTerm` 处报错，故表中不要留空项（推定：空名字解析失败，与其它变量的错误路径一致）。
- 整数排序键用 `Math.Sign(a.Key - b.Key)`，两个差异极大的 long 相减会溢出从而**排序结果可能错误**（源码遗留缺陷，推定但可由算式直接看出）。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
