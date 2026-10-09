# TOINT

- **类别**：式中函数
- **签名**：
  - `int TOINT(str value)`
- **文档来源**：`ecd/docs/translation/Command.md`「### TOINT `<字符串表达式>`」、`ecd/Expression.md` 函数目录（`int TOINT(str value)`）；zh 套件未单独收录该函数（仅在 `zh/Function_and_Preprocessor.md` 中作为替代方案提及）

## 语义

把参数字符串解析为整数并返回。文档说明：只有由半角数字组成的字符串才能数值化，常见无法按数值解释的内容以及含全角字符时返回 `0`；源码没有捕获全部数字解析异常，详见下方边界补充。源码实际支持的语法比文档更宽：还允许开头的 `+`/`-` 符号、`0x` 十六进制与 `0b` 二进制前缀，以及小数点后全为数字的纯小数（直接舍去小数部分返回整数部分）。

与指令版 `TOINT`（把结果写入 `RESULT:0`）不同，这里是表达式内使用的函数形态，返回值直接参与表达式运算。

## 用法

### `int TOINT(str value)`
```erb
A = TOINT("123")        ; A = 123
B = TOINT("-45")        ; B = -45（源码支持符号）
C = TOINT("0x10")       ; C = 16（源码支持十六进制前缀）
D = TOINT("１２３")     ; D = 0（含全角字符）
E = TOINT("12ab")       ; E = 0（尾部有非数字）
F = TOINT("1.5")        ; F = 1（小数部分全为数字时舍去）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:121`（`["TOINT"] = new ToIntMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4496`（`ToIntMethod`），数值解析转调 `Runtime/Script/Parser/LexicalAnalyzer.cs:138` 的 `ReadInt64`

```text
函数 TOINT(s):
    若 s 为 null 或空串: 返回 0
    若 s.Length < LangManager.GetStrlenLang(s):
        # 字符串含多字节(全角)字符时按字节数计算的长度会大于字符数
        返回 0                      # 无条件返回 0，不看内容
    st = CharStream(s)
    若 st.Current 不是数字 且 不是 '+' 且 不是 '-': 返回 0
    若 st.Current 是 '+' 或 '-' 且 st.Next 不是数字: 返回 0
    ret = LexicalAnalyzer.ReadInt64(st, retZero=true)
        # ReadInt64: 识别 "0x"→十六进制、"0b"→二进制 前缀（不采用八进制），
        # 读入数字串，随后可跟 p/P(2为底) 或 e/E(10为底) 的指数部分
    若 st 未到串尾:
        若 st.Current == '.':
            st 前进一位
            若剩余字符中存在非数字: 返回 0   # 必须是纯小数
        否则: 返回 0                          # 尾部有其他内容
    返回 ret
```

## 数字解析边界补充（C#）

共用整数扫描不等于与普通表达式字面量完全相同：进制前缀只在当前字符为 0 时识别，转换函数的带符号字符串入口不会预先剥离符号。指数沿用尾数进制，非零指数经 double 计算；ISNUMERIC 的 NumericCheck 还要求指数标记后立即为数字，因此带符号指数可能被 TOINT 接受而被 ISNUMERIC 判为 0。小数尾部检查没有要求至少一位数字，`"1."` 也会通过对应检查。

这些函数没有包住 ReadInt64/NumericCheck 的所有异常：非法二进制数字、整数转换溢出、指数结果越界等仍可能抛 CodeEE，不能保证一切非法输入都返回 0。常量参数也可能在装载期折叠时触发这些错误。依据：`Runtime/Script/Parser/LexicalAnalyzer.cs:138-326`、`Runtime/Script/Statements/Function/Creator.Method.cs:4505-4534,4762-4791`。完整规则见 [表达式解析边界.md](../language/表达式解析边界.md)。

## 备注

- 文档与源码的差异：ecd 文档称「只有由半角数字组成的字符串才能数值化」，但源码还接受 `+`/`-` 符号、`0x`/`0b` 前缀和纯小数（舍去小数部分）；这些是文档未提及的行为。
- 文档将结果描述为「赋值给 `RESULT:0`」，那是同名指令（`Runtime/Script/Statements/FunctionIdentifier.cs` 注册、`Runtime/Script/Process.ScriptProc.cs` 分发）的形态；本函数形态（`Runtime/Script/Statements/Function/Creator.cs:121`）在表达式中直接返回值。
- 构造函数中 `CanRestructure = true`：参数为常量时会在解析期折叠为常量。
- 空串返回 0 而非报错。
