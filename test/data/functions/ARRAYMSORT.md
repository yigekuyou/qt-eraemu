# ARRAYMSORT

- **类别**：式中函数（EM 私家版扩展）
- **签名**：int ARRAYMSORT(var 排序基准数组, var 数组1, var 数组2...)（各参数为 1～3 维数组变量，第 2 个起可省略）
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 未收录；zh 套件未收录。本条目语义完全由源码翻译得出。

## 语义

多重排序（multi-sort）：以第 1 参数数组元素的升序为基准，把所有传入的数组整体重新排列。

- 第 1 参数：排序基准数组（一次元数组变量）。对其全部元素做升序排序，得到「原下标 → 排序后名次」的置换表。
- 第 2 参数起：跟随重排的数组（可省略，可传任意多个）。这些数组按同一置换表整体重排，即排序后第 i 行的内容来自排序前第 `sortedArray[i]` 个（原下标）位置。
- 支持整数数组与字符串数组；支持 1 维、2 维、3 维数组（2/3 维时按下标 0 方向的行单位重排，其余维度内容随行整体移动）。
- 返回值：成功返回 `1`；异常情况返回 `0`（详见伪代码：基准数组中出现 `long` 范围外数值、某跟随数组长度不足等）。
- 参数必须是变量（数组变量），不能是表达式或常量；第 1 参数必须是一维数组（其他维度的参数可作为跟随数组，见实现）。

注意：基准数组排序时遇 0（整数）或空字符串（字符串）即停止收录（视为数据结束标志），其后的元素不参与排序；跟随数组只有前 `sortedArray.Length` 行会被重写。

## 用法

### int ARRAYMSORT(var 排序基准数组, {var 数组1, var 数组2...})
- `排序基准数组`：一维数组变量，其元素升序排序决定最终顺序。
- `数组1...`：可选，与基准数组一起按相同顺序重排的数组变量。
- 返回值：成功 `1`，失败 `0`。
```erb
; NAME 是字符串数组，SCORE 是整数数组
; 以 SCORE 升序排列，同时把 NAME 一起重排
SCORE:0 = 30
SCORE:1 = 10
SCORE:2 = 20
NAME:0 = '张三'
NAME:1 = '李四'
NAME:2 = '王五'
ARRAYMSORT(SCORE, NAME)
; 结果：SCORE = {10,20,30}，NAME = {'李四','王五','张三'}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:110`（`["ARRAYMSORT"] = new ArrayMultiSortMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4068`（`ArrayMultiSortMethod`）

```text
构造：返回类型 = long；
     argumentTypeArrayEx = [{ RefAny1D, RefAnyArray|Variadic }, OmitStart = 1]
     （第 1 参数为引用形式的一维数组，第 2 参数起为可变个数的数组引用，从第 2 个起可省略）；
     CanRestructure = false；HasUniqueRestructure = true（自制的参数重构处理）。

GetIntValue(exm, args):
    varTerm ← args[0]（必须是 VariableTerm）
    若基准数组是整数数组:
        array ← (long[])基准数组
        sortList ← 空
        对 i = 0 .. array.Length-1:
            若 array[i] == 0: 跳出循环        ; 0 视为数据结束
            若 array[i] 超出 long 范围: 返回 0
            sortList.Add((array[i], i))
        sortList 按 Key（元素值）升序排序
        sortedArray ← sortList 中原下标的序列
    否则（字符串数组）:
        array ← (string[])基准数组
        sortList ← 空
        对 i = 0 .. array.Length-1:
            若 array[i] 为 null 或空串: 跳出循环 ; 空串视为数据结束
            sortList.Add((array[i], i))
        sortList 按 Key（字符串比较）升序排序
        sortedArray ← sortList 中原下标的序列
    对 args 中每个 VariableTerm（含基准数组本身与全部跟随数组）:
        若一维数组:
            clone ← array 的副本
            若 array.Length < sortedArray.Length: 返回 0
            对 i = 0 .. sortedArray.Length-1:
                array[i] ← clone[sortedArray[i]]
        否则若二维数组:
            clone ← array 的副本
            若 array.GetLength(0) < sortedArray.Length: 返回 0
            对 i, x: array[i, x] ← clone[sortedArray[i], x]
        否则若三维数组:
            clone ← array 的副本
            若 array.GetLength(0) < sortedArray.Length: 返回 0
            对 i, x, y: array[i, x, y] ← clone[sortedArray[i], x, y]
        否则:
            抛出 ExeEE（"异常的数组"）   ; 理论上不可达
    返回 1

UniqueRestructure(exm, args):
    对每个参数执行 Restructure，返回 false（本函数不做进一步折叠）
```

## 备注

- 三套文档（ecd/Command.md、ecd/Expression.md、zh 套件）均未收录本函数，属于 EM 私家版（Eraraya 私改版）扩展，语义完全依据源码。
- 源码注释表明：原版只能处理 int 范围，这里「一工夫」改用 long 比较排序；字符串数组遇空串跳出循环也是 EM 私家版修正（原版返回 0）。
- 传入数组的实际类型检查（非变量、角色变量、维度等）由解析期 `argumentTypeArrayEx` 完成；被注释掉的旧 `CheckArgumentType` 表明早期版本还要求第 1 参数必须为一维数组、参数不能是角色变量。
- 排序基准中的 0 / 空字符串作为「结束标志」的行为未见于任何文档，是本实现特有的隐式约定。
