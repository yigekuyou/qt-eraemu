# ISNUMERIC

- **类别**：式中函数
- **签名**：int ISNUMERIC(str value)
- **文档来源**：`ecd/Command.md`「### ISNUMERIC `<字符串表达式>`」；`ecd/Expression.md` 签名列表

## 语义

判断字符串能否被解析为数值（能否用 `TOINT` 取出值）。参数能按数值解释时返回 `1`，否则返回 `0`。

不写入变量；内部仍执行数字扫描及指数范围检查，部分非法输入会抛错。对合法形式的判定比文档描述更宽：源码接受可选的正负号、十进制数字、小数部分（`.` 后接数字），还支持 `0x`/`0X` 开头的十六进制、`0b`/`0B` 开头的二进制以及 `e`/`E`/`p`/`P` 指数形式（与脚本词法分析器的整数解析一致）；含全角字符的字符串一律判为 `0`。

## 用法

### int ISNUMERIC(str value)
- `value`：字符串表达式，待判断的字符串。
- 返回值：可按数值解释时 `1`，否则 `0`。
```erb
IF ISNUMERIC(INPUTS)
  V = TOINT(INPUTS)
ELSE
  PRINTL 请输入数字
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:131`（`["ISNUMERIC"] = new IsNumericMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4754`（`IsNumericMethod`）；调用的 `LexicalAnalyzer.NumericCheck` 在 `Runtime/Script/Parser/LexicalAnalyzer.cs:275`

```text
构造：返回类型 = long；参数 = [string]；CanRestructure = true。

GetIntValue(exm, args):
    s ← args[0].GetStrValue(exm)
    若 s.Length < LangManager.GetStrlenLang(s):   ; 含全角字符（显示宽度 > 字符数）
        返回 0
    st ← CharStream(s)
    若 st.Current 不是数字 且 不是 '+' 且 不是 '-': 返回 0
    若 st.Current 是 '+' 或 '-' 且 st.Next 不是数字: 返回 0
    若 !LexicalAnalyzer.NumericCheck(st): 返回 0   ; 支持进制前缀与指数；指数后无数字判失败；
                                                   ; 指数部分超 Int64 范围会抛 CodeEE
    若 !st.EOS:                                    ; 数字之后还有剩余字符
        若 st.Current == '.':
            st.ShiftNext()
            循环到串尾: 若当前字符不是数字则返回 0     ; 小数点后必须全是数字
        否则: 返回 0
    返回 1
```

## 数字解析边界补充（C#）

共用整数扫描不等于与普通表达式字面量完全相同：进制前缀只在当前字符为 0 时识别，转换函数的带符号字符串入口不会预先剥离符号。指数沿用尾数进制，非零指数经 double 计算；ISNUMERIC 的 NumericCheck 还要求指数标记后立即为数字，因此带符号指数可能被 TOINT 接受而被 ISNUMERIC 判为 0。小数尾部检查没有要求至少一位数字，`"1."` 也会通过对应检查。

这些函数没有包住 ReadInt64/NumericCheck 的所有异常：非法二进制数字、整数转换溢出、指数结果越界等仍可能抛 CodeEE，不能保证一切非法输入都返回 0。常量参数也可能在装载期折叠时触发这些错误。依据：`Runtime/Script/Parser/LexicalAnalyzer.cs:138-326`、`Runtime/Script/Statements/Function/Creator.Method.cs:4505-4534,4762-4791`。完整规则见 [表达式解析边界.md](../language/表达式解析边界.md)。

## 备注

- ecd/Command.md 的描述（"能否用 TOINT 取出值"）与源码基本一致，但未提及源码还接受十六进制/二进制/指数等形式；这些形式 `TOINT` 能否同样解析需以 `TOINT` 实现为准，文档与源码在"接受形式"的范围上存在差异，两边都记录于此。
- 小数形式（如 `"1.5"`）源码判为可解析（返回 1）；而 ecd 文档称 `TOINT` 只有半角数字组成的字符串才能数值化，两者对"能解析"的定义并不完全对齐。
- zh 套件未收录本函数。
