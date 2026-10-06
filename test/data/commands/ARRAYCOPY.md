# ARRAYCOPY

- **类别**：命令
- **签名**：
  - `ARRAYCOPY <复制源变量名>`, `<复制目标变量名>`
- **文档来源**：`ecd/docs/translation/Command.md`「数组操作」相关小节（ARRAYCOPY）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

不加判断地复制数组。把复制源变量的值复制到复制目标变量。

两个变量的类型必须相同（同为数值或同为字符串）、维数也必须相同，且不支持角色变量。元素数不同时，只复制能够复制的部分（按两数组各维中较小的大小复制）。目标变量不能是常量变量。源或目标不是数组变量、不是变量名时出错。

格式示例：`ARRAYCOPY "A", "B"`——参数是**变量名字符串**，不是变量本身。

## 用法

### `ARRAYCOPY <复制源变量名>, <复制目标变量名>`
- `<复制源变量名>`：字符串（变量名），复制数据的来源。
- `<复制目标变量名>`：字符串（变量名），数据写入的目标；不能是常量变量。
```erb
;把数组 A 的内容复制到数组 B
ARRAYCOPY "A", "B"
;元素数不同时只复制能复制的部分（例：B 比 A 大，则 B 的多余元素不变）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:332`（`argb[FunctionArgType.SP_COPY_ARRAY]`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:687`（`case FunctionCode.ARRAYCOPY` 分支）；核心算法在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:782`（`CopyArray`）

```text
case ARRAYCOPY:
    arg = (SpCopyArrayArgument)func.Argument
    若 两个参数不都是（字符串字面量）SingleTerm:
        name1 = 参数1 求值, name2 = 参数2 求值     # 参数可以是字符串表达式
        vars[0] = 按名字查找变量；找不到 → 抛出 CodeEE（"ARRAYCOPY 第1参数不是变量名"）
        若 vars[0] 不是 1~3 维数组 → 抛出 CodeEE（"ARRAYCOPY 第1参数不是数组"）
        若 vars[0] 是角色变量 → 抛出 CodeEE（"ARRAYCOPY 第1参数不能是角色变量"）
        vars[1] = 同上检查第 2 参数
        若 vars[1] 是常量 → 抛出 CodeEE（"ARRAYCOPY 第2参数是常量变量"）
        若 两变量维数不同 → 抛出 CodeEE（"ARRAYCOPY 两参数维数不同"）
        若 类型不同（一整数一字符串）→ 抛出 CodeEE（"ARRAYCOPY 两参数类型不同"）
    否则（两个字面量名）:
        直接按名字取变量（此路径只做类型一致性检查）
    VariableEvaluator.CopyArray(vars[0], vars[1])

CopyArray(var1, var2):
    按 var1 的维数（1D/2D/3D）与类型（整数/字符串）分支：
        对每一维取 min(源维长, 目标维长) 作为复制范围
        逐元素 array2[i, j, ...] = array1[i, j, ...]
    即「只复制能够复制的部分」，目标多余元素保持原值
```

## 备注

- 文档「不支持角色变量」与源码一致：显式检查 `IsCharacterData` 并抛错。
- 文档「类型必须相同、维数必须相同」在源码两条路径中均有检查；但字面量参数（`ARRAYCOPY "A", "B"`）走 else 路径时，源码只检查了类型一致性，维数检查依赖于两条路径共同后的 `CopyArray` 行为——若维数不同会在 `CopyArray` 中因维数分支不匹配出错，语义上仍是禁止的。
- 「元素数不同时只复制能够复制的部分」对应 `CopyArray` 中各维取 min 的实现。
- zh 套件未收录本命令。
