# ARRAYSORT

- **类别**：命令
- **签名**：
  - `ARRAYSORT <目标变量>`{, `<排序方式（FORWARD 或 BACK）>`, `<起始索引>`, `<目标元素数>`}
- **文档来源**：`ecd/docs/translation/Command.md`「数组操作」相关小节（ARRAYSORT）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

对数组变量排序。从起始索引开始，对目标元素数个连续的数组数据排序。`FORWARD` 为升序，`BACK` 为降序；排序方式与起始索引、元素数都可省略，省略排序方式时按升序。

只支持一维数组以及数组型角色变量，不能用于 `DITEMTYPE`、`TA` 等（多维数组）。起始索引为负、越界或 `起始索引 + 元素数` 超出数组时报错。

## 用法

### `ARRAYSORT <目标变量>{, <排序方式>, <起始索引>, <目标元素数>}`
- `<目标变量>`：目标一维数组变量（数值或字符串数组均可）。
- `<排序方式（FORWARD 或 BACK）>`（可省略）：`FORWARD` 升序、`BACK` 降序；省略为升序。
- `<起始索引>`（可省略）：排序起始位置；省略时为 0。为负时报错。
- `<目标元素数>`（可省略）：排序的元素个数；省略时排到数组末尾。为负时报错，为 0 时什么都不做。
```erb
;整组升序
ARRAYSORT FLAG
;整组降序
ARRAYSORT FLAG, BACK
;从索引 10 起对 5 个元素降序
ARRAYSORT FLAG, BACK, 10, 5
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:331`（`argb[FunctionArgType.SP_SORTARRAY]`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:664`（`case FunctionCode.ARRAYSORT` 分支）；核心算法在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:728`（`SortArray`）

```text
case ARRAYSORT:
    arg = (SpArraySortArgument)func.Argument
    若 arg.VarToken.Identifier 不是一维数组:
        抛出 CodeEE（"ARRAYSORT 只能用于一维变量"）
    p = arg.VarToken 的固定变量项
    start = (int)arg.Num1 求值
    若 start < 0: 抛出 CodeEE（"ARRAYSORT 第3参数为负"）
    若 arg.Num2 存在:
        num = (int)arg.Num2 求值
        若 num < 0: 抛出 CodeEE（"ARRAYSORT 第4参数为负"）
        若 num == 0: 什么都不做
    否则:
        num = -1（表示到数组末尾）
    VariableEvaluator.SortArray(p, arg.Order, start, num)

SortArray(p, order, start, count):
    若 order == UNDEF: order = ASCENDING     # 排序方式省略 → 升序
    array = p.Identifier 的底层数组（角色变量则取对应角色的数组）
    若 start >= array.Length: 抛出 CodeEE（"ARRAYSORT 的范围越界"）
    若 count <= 0: count = array.Length - start
    end = start + count
    若 end > array.Length: 抛出 CodeEE（"ARRAYSORT 范围之和越界"）
    对 array[start .. end-1] 这一段排序（升序，数值按大小、字符串按序）
    若 order == DESENDING（BACK）:
        把这一段反转（变为降序）
```

## 备注

- 文档参数为 `FORWARD 或 BACK`，源码内部枚举为 `ASCENDING / DESENDING`，`FORWARD→ASCENDING`、`BACK→DESENDING`。
- 排序是对数组的「一段区间」做原地排序；数值数组按 long 比较，字符串数组按字符串比较。
- 排序方式省略时（`UNDEF`）按升序处理，与文档一致。
- zh 套件未收录本命令。
