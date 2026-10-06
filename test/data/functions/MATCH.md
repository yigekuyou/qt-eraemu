# MATCH

- **类别**：式中函数
- **签名**：
  - `int MATCH(var array, ? value, int start = 0, int end = ※)`
  - `int CMATCH(var carray, ? value, int start = 0, int end = CHARANUM)`（同类的角色数组版，一并记载）
- **文档来源**：`ecd/Expression.md`「内置表达式内函数一览」（仅签名目录）；`ecd/Command.md` 未收录；zh 套件未收录

## 语义

在数组变量 `array` 中统计与 `value` 相等的元素个数并返回。搜索范围为 `[start, end)`（左闭右开）；`start` 默认 0，`end` 默认为数组长度（普通数组）或 `CHARANUM`（角色变量 `CMATCH`）。第 1 参数必须是变量（普通版限一维数组，二维/三维数组不支持）；第 2 参数类型须与第 1 参数一致，即数值数组配数值、字符串数组配字符串。普通版 `MATCH` 会在解析期/求值期校验 `[start, end)` 是否越界，越界抛出 CodeEE；`CMATCH` 则校验角色编号范围，越界同样抛 CodeEE。搜索范围为空（`start >= end`）时返回 0。

## 用法

### MATCH(var array, ? value, int start = 0, int end = 数组长度)

- `array`：被搜索的一维数组变量（数值或字符串均可）。
- `value`：要匹配的值，类型与 `array` 元素一致。
- `start`：起始下标（含），默认 0。
- `end`：结束下标（不含），默认数组长度。

```erb
A = MATCH(FLAG, 3)          ; FLAG 中值等于 3 的元素个数
B = MATCH(NAMES, " Alice ", 0, 10)  ; NAMES 前 10 个元素中 "Alice" 的个数
```

### CMATCH(var carray, ? value, int start = 0, int end = CHARANUM)

- `carray`：角色变量（如 `CFLAG` 等）。
- `value`：要匹配的值。
- `start`：起始角色编号（含），默认 0。
- `end`：结束角色编号（不含），默认 `CHARANUM`。

```erb
C = CMATCH(CFLAG, 5)        ; 所有角色中 CFLAG 等于 5 的人数
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:90`（`["MATCH"] = new MatchMethod()`；`Runtime/Script/Statements/Function/Creator.cs:90` 为 `CMATCH = new MatchMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3333`（`MatchMethod`；辅助 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:317` `GetMatch`、`:340` `GetMatchChara`）

```text
function MATCH / CMATCH(args, exm):
    varTerm = args[0]            ; 必须是变量
    start = args[2] 存在 ? args[2].GetIntValue() : 0
    end   = args[3] 存在 ? args[3].GetIntValue()
            : isCharaRange ? CHARANUM : varTerm.GetLength()
    p = varTerm.GetFixedVariableTerm(exm)
    if not isCharaRange:                     ; MATCH
        p.IsArrayRangeValid(start, end, "MATCH", 3, 4)
        ; 越界（start/end < 0 或 > 数组长度）→ CodeEE
        count = 0
        for i in [start, end):
            if 取得元素值(字符串版为字符串比较；目标为空串时把"未设置"的空元素也算匹配) == value:
                count++
        return count
    else:                                    ; CMATCH
        if start >= CHARANUM or start < 0 or end > CHARANUM or end < 0:
            throw CodeEE(角色范围越界)
        count = 0
        for i in [start, end):               ; i 为角色编号
            if 该角色对应元素 == value: count++
        return count
```

## 备注

- ecd/Command.md 未收录本函数小节，语义依据 `ecd/Expression.md` 签名目录与源码推得。
- 注意 `MATCH` 的范围是左闭右开；字符串匹配时若目标 `value` 为空串，`GetMatch` 会把元素也为空串（含未设置）的情况计入匹配（`Runtime/Script/Statements/Variable/VariableEvaluator.cs:328-338` 的 `targetIsNullOrEmpty` 分支）。
- 解析期类型检查（`CheckArgumentType`）的旧代码在源码中被整体注释，现行版本改用 `argumentTypeArrayEx`（`ArgTypeList`，第 3、4 参数自第 2 项起可省略）。
- `CanRestructure = false`，但实现了 `UniqueRestructure`（对第 1 变量参数做重整，避免该函数被误当作可常量折叠）。
