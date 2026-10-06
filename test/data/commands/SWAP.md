# SWAP

- **类别**：命令
- **签名**：SWAP `<变量1>`, `<变量2>`
- **文档来源**：`ecd/docs/translation/Command.md`（`### SWAP <变量1>, <变量2>` 小节）；Era-Chinese-Documentation 未收录本命令

## 语义

交换两个变量中的值。两个变量的值类型必须一致（数值与数值、字符串与字符串），类型不一致时解析期即告警；运行期再校验一次，仍不一致则抛错。

两个参数都必须是可赋值变量（可带下标）。数组变量之间可以整体交换吗——不行，SWAP 交换单个变量单元（含指定下标后的元素），不是整个数组。

## 用法

### SWAP `<变量1>`, `<变量2>`

- `<变量1>`、`<变量2>`：可赋值变量（数值型或字符串型），二者类型必须一致。
- 副作用：两个变量的值互换。

```erb
A = 1
B = 2
SWAP A, B
PRINTFORML A={A}, B={B}   ;A=2, B=1

STR:0 = "あ"
STR:1 = "い"
SWAP STR:0, STR:1         ;字符串变量也可交换
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:292`（`argb[FunctionArgType.SP_SWAPVAR]`，METHOD_SAFE | EXTENDED）
- 实现：`Runtime/Script/Process.ScriptProc.cs:356`（`case FunctionCode.SWAP`）；参数解析在 `Runtime/Script/Statements/ArgumentBuilder.cs:1706`（`SP_SWAPVAR_ArgumentBuilder`）

```text
参数解析（SP_SWAPVAR_ArgumentBuilder）:
    两个参数都必须是可赋值变量（getChangeableVariable），否则告警返回 null
    两参数的操作数类型不一致 → 告警「两参数类型不匹配」

case SWAP:
    // 1756beta2+v11 注释：必须在读值之前先固定下标，
    // 否则下标中含 RAND 时两次求值会得到不同下标、交换出错
    vTerm1 = arg.var1.GetFixedVariableTerm(exm)   // 求值并固定下标
    vTerm2 = arg.var2.GetFixedVariableTerm(exm)
    若 vTerm1.GetOperandType() != vTerm2.GetOperandType():
        throw new CodeEE(两变量类型不同)
    若操作数类型 == long:
        temp = vTerm1.GetIntValue(exm)
        vTerm1.SetValue(vTerm2.GetIntValue(exm), exm)
        vTerm2.SetValue(temp, exm)
    否则若操作数类型 == string:
        temps = vTerm1.GetStrValue(exm)
        vTerm1.SetValue(vTerm2.GetStrValue(exm), exm)
        vTerm2.SetValue(temps, exm)
    否则:
        throw new CodeEE(未知变量类型)
```

## 备注

- ecd 文档只说「值类型必须一致」；源码在解析期（告警）与执行期（抛 CodeEE）各做一次类型检查，且除 long/string 之外类型直接抛「未知变量类型」错误。
- 实现特意用 `GetFixedVariableTerm` 先固定下标，保证 `SWAP A:RAND:10, B:RAND:10` 这类下标含随机项的写法只求值一次、语义正确（源码注释 1756beta2+v11）。
- Era-Chinese-Documentation 套件未收录本命令。
