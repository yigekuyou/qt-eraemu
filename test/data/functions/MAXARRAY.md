# MAXARRAY

- **类别**：式中函数
- **签名**：
  - `int MAXARRAY(var array, int start = 0, int end = ※)`
  - `int MINARRAY(var array, int start = 0, int end = ※)`
  - `int MAXCARRAY(var carray, int start = 0, int end = CHARANUM)`
  - `int MINCARRAY(var carray, int start = 0, int end = CHARANUM)`
- **文档来源**：`ecd/Expression.md`「内置表达式内函数一览」（仅签名目录，四个名字各自一行）；`ecd/Command.md` 未收录；zh 套件未收录

## 语义

返回一维数值数组在 `[start, end)` 范围内的最大值（`MAXARRAY` / `MAXCARRAY`）或最小值（`MINARRAY` / `MINCARRAY`）。`start` 默认 0；`end` 默认为数组长度（普通版）或 `CHARANUM`（角色版）。只接受整数一维数组变量（角色版为角色变量）。普通版对 `[start, end)` 做数组越界校验，越界抛 CodeEE；角色版对角色编号范围做校验，越界同样抛 CodeEE。四个函数共用同一个实现类，仅由 `isCharaRange`、`isMax` 两个标志区分。

## 用法

### MAXARRAY(var array, int start = 0, int end = 数组长度)

- `array`：整数一维数组变量。
- `start`：起始下标（含），默认 0。
- `end`：结束下标（不含），默认数组长度。

```erb
A = MAXARRAY(DAYS)          ; DAYS 全体元素的最大值
B = MAXARRAY(PALAM, 0, 10)  ; PALAM 前 10 个元素的最大值
```

### MINARRAY(var array, int start = 0, int end = 数组长度)

参数同 `MAXARRAY`，返回最小值。

```erb
C = MINARRAY(DAYS)
```

### MAXCARRAY(var carray, int start = 0, int end = CHARANUM)

- `carray`：整数角色变量（如 `CFLAG`）。
- `start`：起始角色编号（含），默认 0。
- `end`：结束角色编号（不含），默认 `CHARANUM`。

```erb
D = MAXCARRAY(CFLAG)        ; 所有角色 CFLAG 的最大值
```

### MINCARRAY(var carray, int start = 0, int end = CHARANUM)

参数同 `MAXCARRAY`，返回最小值。

```erb
E = MINCARRAY(CFLAG, 0, CHARANUM)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:95`（`MAXARRAY`）、`:96`（`MAXCARRAY = new MaxArrayMethod(true)`）、`:97`（`MINARRAY = new MaxArrayMethod(false, false)`）、`:98`（`MINCARRAY = new MaxArrayMethod(true, false)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3598`（`MaxArrayMethod`；辅助 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:457` `GetMaxArray`、`:478` `GetMaxArrayChara`）

```text
function MAXARRAY/MINARRAY/MAXCARRAY/MINCARRAY(args, exm):
    vTerm = (VariableTerm)args[0]     ; 必须是变量
    start = args[1] 存在 ? args[1].GetIntValue() : 0
    end   = args[2] 存在 ? args[2].GetIntValue()
            : isCharaRange ? CHARANUM : vTerm.GetLength()
    p = vTerm.GetFixedVariableTerm(exm)
    if not isCharaRange:              ; MAXARRAY / MINARRAY
        p.IsArrayRangeValid(start, end, funcName, 2, 3)
        ; start/end < 0 或 > 数组长度 → CodeEE（提示第 2/3 参数越界）
        return GetMaxArray(p, start, end, isMax)
    else:                             ; MAXCARRAY / MINCARRAY
        if start >= CHARANUM or start < 0 or end > CHARANUM or end < 0:
            throw CodeEE(角色范围越界)
        return GetMaxArrayChara(p, start, end, isMax)

function GetMaxArray(p, start, end, isMax):
    ret = 元素[start]
    for i = start+1 to end-1:
        v = 元素[i]
        if (isMax and v > ret) or (not isMax and v < ret): ret = v
    return ret
    ; GetMaxArrayChara 同理，把 i 当作角色编号取值
```

## 备注

- ecd/Command.md 未收录这四个函数的小节，语义依据 `ecd/Expression.md` 签名目录（`end = ※` 表示"数组长度"）与源码推得。
- 注意范围为空（`start == end`）时 `GetMaxArray` 仍会读取下标 `start` 处的元素并返回（先取 `ret = 元素[start]` 再进入循环），与 `MATCH` 空范围返回 0 的行为不同；且此时若 `start` 等于数组长度，行为取决于越界校验已通过与否（校验只到 `end` 边界）。
- 与 `MATCH` 不同，本函数只支持整数数组（`ArgType.RefInt1D`），不支持字符串数组。
- `CanRestructure = false`。
