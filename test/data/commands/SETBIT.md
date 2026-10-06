# SETBIT

- **类别**：命令
- **签名**：`SETBIT <数值型变量>, <数值表达式>{, <数值表达式>, ...}`
- **文档来源**：`ecd/docs/translation/Command.md`（SETBIT / CLEARBIT / INVERTBIT 小节）；`Era-Chinese-Documentation/docs/` 未收录该命令小节

## 语义

位操作指令。把第 1 参数指定的数值型变量中、第 2 参数及之后各表达式所指定位置的位设为 1。

- 第 1 参数必须是可直接赋值的数值型变量（普通变量或数组元素），不能是常数或一般表达式。
- 其后的每个参数是一个 0～63 的整数，指定位的位置（0 为最低位）。
- 参数个数可变，可以一次设置多位。
- 位序号超出 0～63 范围时报错。

同族指令：`CLEARBIT`（对应位清 0）、`INVERTBIT`（对应位反转）。

## 用法

### SETBIT <数值型变量>, <数值表达式>{, <数值表达式>, ...}

- `<数值型变量>`：目标变量。
- `<数值表达式>`（≥1 个）：位序号，0～63。

```erb
SETBIT X, A
CLEARBIT Y, B
INVERTBIT Z, C
```

上述三行分别等价于：

```erb
X |= 1 << A
Y &= ~(1 << B)
Z ^= 1 << C
```

一次设置多位的例子：

```erb
SETBIT FLAG:0, 0, 3, 5    ; 将第 0、3、5 位同时置 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:311`（`new SETBIT_Instruction(1)`；同段：`CLEARBIT → SETBIT_Instruction(0)`、`INVERTBIT → SETBIT_Instruction(-1)`，三者共用实现类）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:718`（`SETBIT_Instruction`，`DoInstruction` 在 727 行）

```text
SETBIT_Instruction(op):          // SETBIT: op=1, CLEARBIT: op=0, INVERTBIT: op=-1
    构造: ArgBuilder = BIT_ARG（第 1 参数解析为变量引用，其余为表达式列表）
          标记 METHOD_SAFE | EXTENDED

    DoInstruction(exm, func, state):
        spsetarg = (BitArgument)func.Argument
        varTerm = spsetarg.VariableDest      // 目标变量引用
        terms = spsetarg.Term                // 位序号表达式数组
        for i in 0 .. terms.Length-1:
            x = terms[i].GetIntValue(exm)
            if x < 0 or x > 63:
                # 源码: string.Format(trerror.ArgIsOoRBit.Text, "2")
                # 该文案含 {0}、{1} 两个占位符（zhs「第{0}参数({1})超出范围(0-63)」），
                # 却只传入 1 个实参 → 实际抛 FormatException（并非预期的 CodeEE）
                throw ...
            baseValue = varTerm.GetIntValue(exm)   // 每次循环重新读取当前值
            shift = 1L << x
            if   op ==  1: baseValue |= shift      // SETBIT：置 1
            elif op ==  0: baseValue &= ~shift     // CLEARBIT：清 0
            else:          baseValue ^= shift      // INVERTBIT：反转
            varTerm.SetValue(baseValue, exm)       // 写回目标变量
```

## 备注

- 三条位指令共用一个实现类，仅构造参数 `op` 不同（1/0/-1），语义上分别对应 `|=`、`&=~`、`^=`。
- 实现细节：多位参数时逐位「读取→修改→写回」，前一位的结果对下一位可见，最终效果与一次性组合相同。
- 源码运行期越界分支写作 `string.Format(trerror.ArgIsOoRBit.Text, "2")`：`ArgIsOoRBit` 文案含 `{0}`、`{1}` 两个占位符（如 zhs「第{0}参数({1})超出范围(0-63)」），这里只传入 1 个实参，因此实际抛出的是 `FormatException` 而非 `CodeEE`；且固定写死 `"2"`，对第 3 个及以后的位序号参数也不准确。（常量位序号在解析期就被 `BIT_ARG_ArgumentBuilder` 拦截：`warn(string.Format(..., i + 2, bit), ...)` 给出带正确参数序号的警告并中止本行解析，见 `Runtime/Script/Statements/ArgumentBuilder.cs:1806`；运行期分支只对非常量表达式生效。）
- `zh/` 文档套件未收录本命令，无从交叉核对。
