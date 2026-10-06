# CLEARBIT

- **类别**：命令
- **签名**：`CLEARBIT <数值型变量>, <数值表达式>{, <数值表达式>, ...}`
- **文档来源**：`ecd/docs/translation/Command.md`「CLEARBIT」小节；`Era-Chinese-Documentation` 未收录本命令

## 语义

位操作指令。把第 1 参数指定的变量中、第 2 参数及之后各表达式指定位置的位清零（设为 0）。等价于对变量执行 `变量 &= ~(1 << 位)`。位编号范围是 0～63；越界会报运行时错误。可与 `GETBIT(变量, 位)` 函数配合读取结果。第 1 参数必须是可赋值的变量（带下标），后续参数个数任意，按顺序逐个清位。

## 用法

### `CLEARBIT <数值型变量>, <位表达式>{, <位表达式>, ...}`
- 第 1 参数：目标整型变量（可含数组下标）。
- 第 2 参数起：要清零的位编号，0～63，可写多个。
```erb
Y = 255
CLEARBIT Y, 0, 1   ; 把 Y 的第 0、1 位清零 → Y = 252
;等价于 Y &= ~(1 << 0)；Y &= ~(1 << 1)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:312` → `new SETBIT_Instruction(0)`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:718`（`SETBIT_Instruction`，op = 0）

```text
DoInstruction(exm, func, state):
    arg = (BitArgument)func.Argument
    varTerm = arg.VariableDest          // 第 1 参数解析成的变量引用
    terms   = arg.Term                  // 其余各位编号表达式
    对每个 terms[i]:
        x = terms[i].GetIntValue(exm)
        若 x < 0 或 x > 63:
            # 源码: string.Format(trerror.ArgIsOoRBit.Text, "2")
            # 该文案含 {0}、{1} 两个占位符却只传 1 个实参 → 实际抛 FormatException（非 CodeEE）；
            # 常量位序号在解析期已被 BIT_ARG_ArgumentBuilder 拦截（警告并中止解析）
            抛出异常
        baseValue = varTerm.GetIntValue(exm)             // 每次循环都重新读当前值
        shift = 1L << x
        op == 1 → baseValue |= shift    // SETBIT
        op == 0 → baseValue &= ~shift   // CLEARBIT（本命令）
        其他    → baseValue ^= shift    // INVERTBIT
        varTerm.SetValue(baseValue, exm)
```

## 备注

- `CLEARBIT`、`SETBIT`、`INVERTBIT` 三者共用同一个 `SETBIT_Instruction` 类，仅构造参数 op 不同（0/1/其他），注册行分别为 `Runtime/Script/Statements/FunctionIdentifier.cs:311-313`。
- ecd 文档说位操作结果与 `Y &= ~(1 << B)` 等价（CLEARBIT 对应的运算式），与源码一致。注意实现是"读一次、改一位、写一次"循环进行，同一位写多次时后面的覆盖前面，且变量在循环中途被更新的写法不会被优化合并。
- zh 文档套件未收录该命令。
