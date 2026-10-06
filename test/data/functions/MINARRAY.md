# MINARRAY

- **类别**：式中函数
- **签名**：
  - `int MINARRAY(var array, int start = 0, int end = ※)`
- **文档来源**：`ecd/Expression.md`「内置表达式内函数一览」（仅签名目录）；`ecd/Command.md` 未收录；zh 套件未收录

## 语义

返回一维整数数组在 `[start, end)` 范围内的最小值。`start` 默认 0，`end` 默认为数组长度。范围越界（`start`/`end` 小于 0 或大于数组长度）时抛出 CodeEE。与 `MAXARRAY` 是同一实现类的不同实例（`isMax = false`）。

## 用法

### MINARRAY(var array, int start = 0, int end = 数组长度)

- `array`：整数一维数组变量。
- `start`：起始下标（含），默认 0。
- `end`：结束下标（不含），默认数组长度。

```erb
A = MINARRAY(DAYS)              ; DAYS 全体元素的最小值
B = MINARRAY(PALAM, 2, 8)       ; PALAM[2]～PALAM[7] 的最小值
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:97`（`["MINARRAY"] = new MaxArrayMethod(false, false)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3598`（`MaxArrayMethod`，`isCharaRange = false, isMax = false`；辅助 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:457` `GetMaxArray`）

```text
function MINARRAY(args, exm):
    vTerm = (VariableTerm)args[0]
    start = args[1] 存在 ? args[1].GetIntValue() : 0
    end   = args[2] 存在 ? args[2].GetIntValue() : vTerm.GetLength()
    p.IsArrayRangeValid(start, end, "MINARRAY", 2, 3)   ; 越界 → CodeEE
    ret = 元素[start]
    for i = start+1 to end-1:
        if 元素[i] < ret: ret = 元素[i]
    return ret
```

## 备注

- ecd/Command.md 未收录本函数；语义依据 `ecd/Expression.md` 签名目录与源码推得。
- 空范围（`start == end`）时仍会读取下标 `start` 的元素并返回，参见 `MAXARRAY.md` 备注。
- 只支持整数一维数组；不支持字符串数组和二重/三重数组。
