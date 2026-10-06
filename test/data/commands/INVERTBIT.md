# INVERTBIT

- **类别**：命令
- **签名**：
  - `INVERTBIT <数值型变量>, <数值表达式>{, <数值表达式>,...}`
- **文档来源**：`ecd/docs/translation/Command.md`「算术」INVERTBIT 小节；zh 套件未收录本命令（`zh/` 目录中无任何提及）。

## 语义

位操作指令。把第 1 参数指定的数值型变量中、第 2 参数及之后指定的各个位**反转**（0 变 1、1 变 0）。等价于对每个位位置 i 执行 `变量 ^= 1 << i`。

同一行可以指定任意多个位，按参数顺序依次处理。与 `SETBIT`（置 1）、`CLEARBIT`（置 0）成对；被 `INVERTBIT Z, C` 修改过的位可用 `GETBIT(Z, C)` 函数读取。

## 用法

### `INVERTBIT <数值型变量>, <数值表达式>{, <数值表达式>,...}`
- `<数值型变量>`：要修改的变量（必须可赋值，支持数组元素、角色变量等变量目标）。
- `<数值表达式>`：位位置，有效范围 0～63；可重复多个，每个位独立反转。
```erb
X = 0
INVERTBIT X, 3      ;X = 0b1000  （第 3 位 0→1）
INVERTBIT X, 3      ;X = 0       （第 3 位 1→0）
INVERTBIT X, 0, 2   ;第 0、2 位同时反转
```
等价关系（文档示例）：
```erb
;INVERTBIT Z, C 与下面一行等价
Z ^= 1 << C
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:313`（`new SETBIT_Instruction(-1)`，`METHOD_SAFE | EXTENDED`；与 SETBIT(1)、CLEARBIT(0) 共用一个类）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:718`（`SETBIT_Instruction`）；参数解析在 `Runtime/Script/Statements/ArgumentBuilder.cs:1779`（`BIT_ARG_ArgumentBuilder`）

```text
参数解析（BIT_ARG_ArgumentBuilder，minArg=2，argAny）:
    第 1 项必须是可以赋值的变量目标（getChangeableVariable），否则解析失败
    其余各项构成位位置列表；常量位位置不在 0..63 → 解析期警告并失败

DoInstruction(exm, func, state):
    spsetarg = (BitArgument)func.Argument
    varTerm = spsetarg.VariableDest
    对 spsetarg.Term 中的每个位位置表达式 terms[i]：
        x = terms[i].GetIntValue(exm)
        若 x < 0 或 x > 63:
            # 运行期也校验：源码为 string.Format(trerror.ArgIsOoRBit.Text, "2")，
            # 该文案含 {0}、{1} 两个占位符却只传 1 个实参 → 实际抛 FormatException（非 CodeEE）
            抛出异常
        baseValue = varTerm.GetIntValue(exm)     # 逐位“读-改-写”
        shift = 1L << x
        op == 1  → baseValue |= shift    # SETBIT
        op == 0  → baseValue &= ~shift   # CLEARBIT
        否则     → baseValue ^= shift    # INVERTBIT（op = -1）
        varTerm.SetValue(baseValue, exm)
```

## 备注

- 文档等价式 `Z ^= 1 << C` 与源码 `op = -1 → XOR` 分支完全一致。
- 源码按 `op == 1 / op == 0 / else` 三分支实现，`INVERTBIT` 走 else（XOR）分支——任何非 0/1 的 op 都是 XOR，但注册处只有 -1 这一种。
- ecd 的位操作指令小节未提及位位置上限（`GETBIT` 函数小节记有「第 2 参数可指定 0～63」）；源码同为 0～63：常量位序号在解析期由 `BIT_ARG_ArgumentBuilder` 警告并中止本行解析（`Runtime/Script/Statements/ArgumentBuilder.cs:1799-1810`），非常量表达式留到运行期校验。
- 每个位是独立“读-改-写”：前一个位修改的结果对后续位可见（同一行 `INVERTBIT X, 1, 2` 中第 2 位看到的是已反转第 1 位后的值，但由于是异或操作，各位互不干扰，结果与同时反转一致）。
