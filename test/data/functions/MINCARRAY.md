# MINCARRAY

- **类别**：式中函数
- **签名**：
  - `int MINCARRAY(var carray, int start = 0, int end = CHARANUM)`
- **文档来源**：`ecd/Expression.md`「内置表达式内函数一览」（仅签名目录）；`ecd/Command.md` 未收录；zh 套件未收录

## 语义

在整数角色变量的角色编号范围 `[start, end)` 内查找最小值并返回。`start` 默认 0，`end` 默认为 `CHARANUM`。角色编号范围超出 `[0, CHARANUM]` 时抛出 CodeEE。与 `MAXCARRAY` 是同一实现类的不同实例（`isMax = false`）。

## 用法

### MINCARRAY(var carray, int start = 0, int end = CHARANUM)

- `carray`：整数角色变量（如 `CFLAG`）。
- `start`：起始角色编号（含），默认 0。
- `end`：结束角色编号（不含），默认 `CHARANUM`。

```erb
A = MINCARRAY(CFLAG)            ; 全部角色 CFLAG 的最小值
B = MINCARRAY(CEXP, 1, CHARANUM); 跳过主角后的最小值
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:98`（`["MINCARRAY"] = new MaxArrayMethod(true, false)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3598`（`MaxArrayMethod`，`isCharaRange = true, isMax = false`；辅助 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:478` `GetMaxArrayChara`）

```text
function MINCARRAY(args, exm):
    vTerm = (VariableTerm)args[0]
    start = args[1] 存在 ? args[1].GetIntValue() : 0
    end   = args[2] 存在 ? args[2].GetIntValue() : CHARANUM
    if start >= CHARANUM or start < 0 or end > CHARANUM or end < 0:
        throw CodeEE(角色范围越界)
    ret = 元素[start]
    for i = start+1 to end-1:
        if 元素[i] < ret: ret = 元素[i]
    return ret
```

## 备注

- ecd/Command.md 未收录本函数；语义依据 `ecd/Expression.md` 签名目录与源码推得。
- 空范围时仍会读取下标 `start` 的元素并返回，参见 `MAXCARRAY.md` 备注。
