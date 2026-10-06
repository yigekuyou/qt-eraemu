# MIN

- **类别**：式中函数
- **签名**：
  - `int MIN(int n, int m...)`
- **文档来源**：`ecd/Command.md`「算术」→「MIN」小节；`ecd/Expression.md`「内置表达式内函数一览」；zh 套件未收录

## 语义

返回所有参数中的最小值。参数个数不限，但至少要 1 个，全部为数值（64 位整数）。与 `MAX` 是同一实现类的两个实例，仅极性相反。

## 用法

### MIN(int n, int m...)

- `n`：第 1 个数值表达式（不可省略）。
- `m...`：后续任意个数值表达式。

```erb
N = MIN(A, B, C)      ; N = A、B、C 中的最小值
X = MIN(MAX(X, 0), 100)  ; 与 MAX 组合实现区间截断（等价于 LIMIT）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:75`（`["MIN"] = new MaxMethod(false)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3010`（`MaxMethod`，`isMax = false`）

```text
function MIN(args, exm):
    ret = args[0].GetIntValue(exm)
    for i = 1 to args.Count - 1:
        v = args[i].GetIntValue(exm)
        if v < ret: ret = v
    return ret
```

## 备注

- ecd 文档把 `MIN` 记在「算术」命令表中（"把参数中的最小值赋值给 RESULT:0"），实现在函数注册表中；式中调用直接返回值，不经过 `RESULT:0`。
- `CanRestructure = true`：参数全为常量时解析期折叠。
- 需要"把值夹在区间内"时也可用 `LIMIT` 函数。
