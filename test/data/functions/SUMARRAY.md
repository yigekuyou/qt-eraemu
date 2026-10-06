# SUMARRAY

- **类别**：式中函数
- **签名**：int SUMARRAY(var array, int start = 0, int end = ※)
- **文档来源**：`ecd/Expression.md`（签名表，※ = 数组末尾；正文无详解小节）

## 语义

计算数值数组变量在区间 `[start, end)`（含头不含尾）内各元素的总和并返回。`start` 默认 0，`end` 默认为数组该维的末尾（即全部元素）。对多维数组，是固定前维下标后沿**最后一维**求和（如 `SUMARRAY(A:1, 0, 5)` 对 `A:1:0` 到 `A:1:4` 求和）。`start`、`end` 越界（不在 0..该维大小）时抛出运行时错误。

返回值类型为 64 位整数。不修改任何数据，无副作用。

## 用法

### int SUMARRAY(var array, int start = 0, int end = ※)

- `array`：数值数组变量（1~3 维；可带前维固定下标）。
- `start`：起始下标（含），可省略，默认 0。
- `end`：终止下标（不含），可省略，默认数组该维末尾（文档记作 ※）。

```erb
DIM ARR = 1, 2, 3, 4, 5
A = SUMARRAY(ARR)         ; A = 15
B = SUMARRAY(ARR, 1, 3)   ; B = 5（2 + 3，end 不含）
C = SUMARRAY(ARR, 2)      ; C = 12（3 + 4 + 5）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:88`（`["SUMARRAY"] = new SumArrayMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3260`（`private sealed class SumArrayMethod`，`isCharaRange=false`），求和在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:225`（`GetArraySum`）

```text
函数 SUMARRAY(varTerm, start = 0, end):
    若未给 end:
        end = varTerm.GetLastLength()        ; 最后一维长度
    p = varTerm.GetFixedVariableTerm(exm)    ; 解析前维固定下标
    p.IsArrayRangeValid(start, end, "SUMARRAY", 2, 3)
        ; start 或 end 不在 0..该维大小 → CodeEE 越界错误
    返回 VariableEvaluator.GetArraySum(p, start, end)

GetArraySum(p, index1, index2):
    sum = 0
    对 i in index1 .. index2-1:              ; 区间含头不含尾
        sum += 元素值                        ; 下标沿最后一维：
        ;  1 维数组: [i]
        ;  2 维数组: [p.Index1, i]
        ;  3 维数组: [p.Index1, p.Index2, i]
        ;  （角色变量按 [p.Index1, i] / [p.Index1, p.Index2, i] 处理）
    返回 sum
```

## 备注

- `ecd/Expression.md` 只给出签名（`int SUMARRAY(var array, int start = 0, int end = ※)`，※ 意为「数组变量长度」），无正文详解；语义按源码确认：区间为半开区间 `[start, end)`。
- 与 `SUMCARRAY` 共用同一实现类，仅构造参数 `isCharaRange` 不同；`SUMCARRAY` 的 `end` 默认是 `CHARANUM` 且有专门的越界检查，见其文档。
- 逆向用法：`MINARRAY`/`MAXARRAY` 与本函数同族（本仓库中 MINARRAY 复用 `MaxArrayMethod`），但求和只有 SUMARRAY/SUMCARRAY 两个。
