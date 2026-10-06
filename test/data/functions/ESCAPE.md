# ESCAPE

- **类别**：式中函数
- **签名**：str ESCAPE(str value)
- **文档来源**：`ecd/Command.md`「### ESCAPE `<字符串>`」小节；`ecd/Expression.md`「内置表达式内函数一览」`str ESCAPE(str value)`；zh 套件未收录

## 语义

对字符串进行正则表达式转义：把参数中的正则表达式元字符（如 `(`、`)`、`[`、`]`、`$`、`.`、`*`、`+`、`?`、`\` 等）全部加上转义后返回，使该字符串在正则表达式中被当作普通文本处理。典型用途是把用户输入或任意字符串安全地传给 `REPLACE`、`STRCOUNT`、`FINDELEMENT` 等内部使用正则的函数。

实现上直接调用 .NET 的 `Regex.Escape`，转义规则遵循 C# 正则表达式规范。

## 用法

### str ESCAPE(str value)
- `value`：待转义的字符串。
- 返回值：元字符被转义后的字符串。
```erb
; 在 REPLACE 中把含有正则元字符的字符串当普通文本查找
STR = 1+1=2
PRINTL %REPLACE(STR, ESCAPE("1+1"), "two")%   ; 输出 two=2（未转义时 + 是元字符）
PRINTL %ESCAPE("a.b(c)")%                     ; 输出 a\.b\(c\)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:132`（`["ESCAPE"] = new EscapeMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4795`（`EscapeMethod`）

```text
EscapeMethod:
构造：返回类型 = string；参数表 = [string]（1 个参数，不可省略）；
     CanRestructure = true。
GetStrValue(exm, args):
    返回 Regex.Escape(args[0].GetStrValue(exm))
```

## 备注

- ecd/Command.md 与 Expression.md 的描述、签名和源码一致，无冲突。
- `CanRestructure = true`：参数全为常量时可在解析期预先求值。
- 与 `HTML_ESCAPE`（HTML 实体转义，`HtmlEscapeMethod`）不同，本函数只针对正则表达式元字符。
