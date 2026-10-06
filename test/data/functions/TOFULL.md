# TOFULL

- **类别**：式中函数
- **签名**：str TOFULL(str value)
- **文档来源**：`ecd/Expression.md`（签名表）、`ecd/Command.md`「### TOFULL」

## 语义

把参数字符串中的**半角字符转换为全角**后返回。没有对应全角字符的字符保持原样。转换由 .NET 的 `StrConv(str, VbStrConv.Wide, 语言)` 完成，按 `Config.Language` 指定的区域设置进行。

也可以写成独立命令形式 `TOFULL <字符串表达式>`，结果存入 `RESULTS:0`。

## 用法

### str TOFULL(str value)

- `value`：要转换的字符串表达式。

```erb
STR = TOFULL("ABC123")     ; STR = "ＡＢＣ１２３"
STR = TOFULL("ｱｲｳ")         ; STR = "アイウ"
STR = TOFULL("漢字!")      ; "漢字"无变化，"!" → "！"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:125`（`["TOFULL"] = new StrChangeStyleMethod(StrFormType.Full)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4547`（`private sealed class StrChangeStyleMethod`，`strType = StrFormType.Full`）

```text
函数 TOFULL(value):                       ; StrFormType.Full 分支
    若 value 为 null 或空串:
        返回 ""
    返回 Microsoft.VisualBasic.Strings.StrConv(value, VbStrConv.Wide, Config.Language)
        ; Wide = 半角 → 全角；无全角对应的字符由 StrConv 保持原样
```

## 备注

- 与 `TOUPPER`/`TOLOWER`/`TOHALF` 共用 `StrChangeStyleMethod` 类，仅 `StrFormType` 枚举不同（Upper=0, Lower=1, Half=2, Full=3）。
- 文档说「没有对应全角字符的全角字符保持原样」——按文档上下文应为"没有对应全角字符的字符保持原样"，是文档措辞的小瑕疵；`StrConv` 的实际行为与之相符（无法宽字符化的字符原样保留）。
- 转换结果依赖 `Config.Language` 区域设置，不同语言环境下个别字符的转换可能不同（文档未提及此点）。
