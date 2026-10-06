# TOHALF

- **类别**：式中函数
- **签名**：str TOHALF(str value)
- **文档来源**：`ecd/Expression.md`（签名表）、`ecd/Command.md`「### TOHALF」

## 语义

把参数字符串中的**全角字符转换为半角**后返回。没有对应半角字符的全角字符保持原样。转换由 .NET 的 `StrConv(str, VbStrConv.Narrow, 语言)` 完成，按 `Config.Language` 指定的区域设置进行。

也可以写成独立命令形式 `TOHALF <字符串表达式>`，结果存入 `RESULTS:0`。

## 用法

### str TOHALF(str value)

- `value`：要转换的字符串表达式。

```erb
STR = TOHALF("ＡＢＣ１２３")   ; STR = "ABC123"
STR = TOHALF("アイウ")          ; STR = "ｱｲｳ"
STR = TOHALF("漢字！")          ; "漢字"无对应半角，保持原样；"！" → "!"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:124`（`["TOHALF"] = new StrChangeStyleMethod(StrFormType.Half)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4547`（`private sealed class StrChangeStyleMethod`，`strType = StrFormType.Half`）

```text
函数 TOHALF(value):                       ; StrFormType.Half 分支
    若 value 为 null 或空串:
        返回 ""
    返回 Microsoft.VisualBasic.Strings.StrConv(value, VbStrConv.Narrow, Config.Language)
        ; Narrow = 全角 → 半角；无半角对应的字符由 StrConv 保持原样
```

## 备注

- 与 `TOUPPER`/`TOLOWER`/`TOFULL` 共用 `StrChangeStyleMethod` 类，仅 `StrFormType` 枚举不同（Upper=0, Lower=1, Half=2, Full=3）。
- 文档描述与源码行为一致，无冲突；转换依赖 `Config.Language` 区域设置（文档未提及）。
