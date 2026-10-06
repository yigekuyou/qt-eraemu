# MAXCARRAY

- **类别**：式中函数
- **签名**：
  - `int MAXCARRAY(var carray, int start = 0, int end = CHARANUM)`
  - `int MINCARRAY(var carray, int start = 0, int end = CHARANUM)`（同类的最小值版，一并记载）
- **文档来源**：`ecd/Expression.md`「内置表达式内函数一览」（仅签名目录）；`ecd/Command.md` 未收录；zh 套件未收录

## 语义

在角色变量的角色编号范围 `[start, end)` 内查找整数元素的最大值（`MAXCARRAY`）或最小值（`MINCARRAY`）并返回。`start` 默认 0，`end` 默认为 `CHARANUM`（当前登场的角色数量）。只接受整数角色变量。角色编号范围超出 `[0, CHARANUM]` 时抛出 CodeEE。实现与 `MAXARRAY`/`MINARRAY` 共用同一类。

## 用法

### MAXCARRAY(var carray, int start = 0, int end = CHARANUM)

- `carray`：整数角色变量（如 `CFLAG`、`CEXP` 等）。
- `start`：起始角色编号（含），默认 0。
- `end`：结束角色编号（不含），默认 `CHARANUM`。

```erb
A = MAXCARRAY(CFLAG)              ; 全部角色 CFLAG 的最大值
B = MAXCARRAY(CEXP, 0, CHARANUM)  ; 显式指定范围
```

### MINCARRAY(var carray, int start = 0, int end = CHARANUM)

参数同 `MAXCARRAY`，返回最小值。

```erb
C = MINCARRAY(CFLAG)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:96`（`["MAXCARRAY"] = new MaxArrayMethod(true)`）、`:98`（`["MINCARRAY"] = new MaxArrayMethod(true, false)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3598`（`MaxArrayMethod`，`isCharaRange = true`；辅助 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:478` `GetMaxArrayChara`）

```text
function MAXCARRAY/MINCARRAY(args, exm):
    vTerm = (VariableTerm)args[0]
    start = args[1] 存在 ? args[1].GetIntValue() : 0
    end   = args[2] 存在 ? args[2].GetIntValue() : CHARANUM
    if start >= CHARANUM or start < 0 or end > CHARANUM or end < 0:
        throw CodeEE(函数名 + 角色范围越界)
    ret = 元素[start]              ; 按角色编号 start 取值
    for i = start+1 to end-1:
        v = 元素[i]
        if (isMax and v > ret) or (not isMax and v < ret): ret = v
    return ret
```

## 备注

- ecd/Command.md 未收录本函数；语义依据 `ecd/Expression.md` 签名目录与源码推得。
- 空范围（`start == end`）时仍会先读取下标 `start` 的元素作为初值返回，参见 `MAXARRAY.md` 备注的同类说明。
- 只支持整数角色变量；字符串角色变量在解析期报错。
