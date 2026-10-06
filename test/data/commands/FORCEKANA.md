# FORCEKANA

- **类别**：命令
- **签名**：`FORCEKANA <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「文字样式相关」`### FORCEKANA` 小节；`Era-Chinese-Documentation`（zh 套件）未收录该命令

## 语义

指定含关键字 `K` 的 PRINT 系指令（如 `PRINTK`、`PRINTLK` 等）输出平假名还是片假名。参数含义：0 = 无变化、1 = 平假名→片假名、2 = 片假名→平假名（仅全角）、3 = 片假名→平假名（全角半角都转）。设置后一直生效，直到再次调用 `FORCEKANA` 改变。参数超出 0~3 范围时抛出运行时错误。

## 用法

### FORCEKANA <数值表达式>
- `<数值表达式>`：0~3 的整数，含义见上。

```erb
FORCEKANA 1
PRINTK ひらがな      ;以片假名输出
FORCEKANA 2
PRINTK カタカナ      ;全角片假名转回平假名
FORCEKANA 0
PRINTK ひらがな      ;恢复原样输出
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:322`（`argb[FunctionArgType.INT_EXPRESSION]`，flag = METHOD_SAFE | EXTENDED；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:246`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:566`（switch-case，无独立类）；核心逻辑在 `Runtime/Script/Statements/ExpressionMediator.cs:33`（`ExpressionMediator.ForceKana`）

```text
# Process.ScriptProc.cs
case FORCEKANA:
    iValue ← 参数为常量 ? 常量值 : 求值表达式
    exm.ForceKana(iValue)

# ExpressionMediator.ForceKana(flag)
若 flag < 0 或 flag > 3:
    throw CodeEE("FORCEKANA 参数超出范围")
forceKatakana ← (flag == 1)     # 1: 强制片假名
forceHiragana ← (flag > 1)      # 2、3: 强制平假名
halftoFull    ← (flag == 3)     # 3: 半角也一并转换（半角片假名→全角平假名）
# 0: 三者全部为 false，即不做任何转换
```

这三个布尔标记随后被 PRINT 系输出的假名转换逻辑读取（`ExpressionMediator.ForceKana()` 无参重载返回"是否处于强制状态"）。

## 备注

- ecd 文档写"参数为数值，具体如下：0:无变化；1:平假名→片假名；2:片假名→平假名（只有全角）；3:片假名→平假名（全角半角都有）"，与源码语义一致；"只有全角/全角半角都有"对应源码的 `halftoFull` 标志。
- 源码中参数越界会抛 `CodeEE`（trerror.OoRForcekanaArg），ecd 文档未提及该错误行为。
- 其余无冲突。
