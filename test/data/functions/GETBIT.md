# GETBIT

- **类别**：式中函数
- **签名**：int GETBIT(int n, int m)
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；`ecd/Command.md`「### GETBIT `<数值表达式>`, `<数值表达式>`」

## 语义

取出整数 `n` 的第 `m` 位（0 为最低位），返回该位的值（0 或 1）。等价于 `(n >> m) & 1`。

第 2 参数 `m` 可指定 0～63；超出范围会抛出 CodeEE 运行期错误。

文档中给出的等价写法：当第 2 参数是常量 5 时，`GETBIT X, 5` 与 `RESULT = (X & 1p5) != 0` 结果相同（`1p5` 是 2 进制字面量，即把 X 的第 5 位取出后再转成 0/1）。

## 用法

### int GETBIT(int n, int m)
- `n`：目标数值表达式。
- `m`：要取出的位的位置，0～63；越界报错。
- 返回值：`n` 第 `m` 位的值（0 或 1）。
```erb
X = 0b1010
A = GETBIT(X, 1)   ; A = 1
B = GETBIT(X, 2)   ; B = 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:99`（`["GETBIT"] = new GetbitMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3692`（`GetbitMethod`）

```text
构造：返回类型 = long；参数 = [long, long]；CanRestructure = true（允许常量折叠）。

GetIntValue(exm, args):
    n ← args[0].GetIntValue(exm)
    m ← args[1].GetIntValue(exm)
    若 m < 0 或 m > 63:
        抛出 CodeEE（"第 2 参数 m 超出范围 0～63"）
    返回 (n >> (int)m) & 1
```

## 备注

- ecd/Command.md 的小节以「赋值给 `RESULT:0`」的命令式口吻描述；实际注册形态是式中函数，在表达式中调用并返回值，两种描述语义一致。
- 文档说第 2 参数超出 0～63 范围"会出错"，与源码抛出 CodeEE 一致。
- zh 套件（Era-Chinese-Documentation）未收录本函数。
