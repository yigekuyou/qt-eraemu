# MAX

- **类别**：式中函数
- **签名**：
  - `int MAX(int n, int m...)`
  - `int MIN(int n, int m...)`（同类的最小值版，一并记载）
- **文档来源**：`ecd/Command.md`「算术」→「MAX」「MIN」小节；`ecd/Expression.md`「内置表达式内函数一览」；zh 套件未收录

## 语义

返回所有参数中的最大值（`MAX`）/ 最小值（`MIN`）。参数个数不限，但至少要 1 个，全部为数值（64 位整数）。参数可以是任意数值表达式。此函数无副作用，仅在表达式中求值；ecd 文档以命令形式记载（结果赋给 `RESULT:0`），实际既可作命令用也可在式中直接调用，本文件以式中函数形态为主。

## 用法

### MAX(int n, int m...)

- `n`：第 1 个数值表达式（不可省略）。
- `m...`：后续任意个数值表达式。

```erb
M = MAX(A, B, C, E, D, F, G)   ; M = A~G 中的最大值
X = MAX(X, 0)                  ; 常用写法：下限截断
```

### MIN(int n, int m...)

- 参数同 `MAX`。

```erb
N = MIN(A, B, 100)             ; N = A、B、100 中的最小值
X = MIN(X, 100)                ; 常用写法：上限截断
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:76`（`["MIN"] = new MaxMethod(false)`）、`Runtime/Script/Statements/Function/Creator.cs:76`（`["MAX"] = new MaxMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3010`（`MaxMethod`）

```text
function MAX/MIN(args, exm):        ; isMax 区分二者，同一实现类
    ret = args[0].GetIntValue(exm)
    for i = 1 to args.Count - 1:
        v = args[i].GetIntValue(exm)
        if isMax:
            if v > ret: ret = v
        else:
            if v < ret: ret = v
    return ret
```

## 备注

- ecd 文档把 `MAX`/`MIN` 记在「算术」命令表中（"把参数中的最大值赋值给 RESULT:0"），实现在函数注册表中，是典型的"既是命令形态又是式中函数"条目；式中调用不经过 `RESULT:0`。
- `CanRestructure = true`：参数全为常量时可在解析期直接折叠为常量。
- 参数至少 1 个；类型检查器要求全部为 `Int64`，传字符串会解析期报错。
