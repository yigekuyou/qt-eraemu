# STRLENSU

- **类别**：式中函数
- **签名**：int STRLENSU(str s)
- **文档来源**：`ecd/Expression.md`（签名表）、`ecd/Command.md`「### STRLENSU」

## 语义

`STRLENS` 的 Unicode 版：返回字符串 `s` 的字符数（.NET 字符串长度），全角字符也按 1 个字符计算，不做任何编码字节换算。

也可以写成独立命令形式 `STRLENSU <表达式>`，此时返回值存入 `RESULT:0`。与命令 `STRLENU`（`STRLEN` 的 Unicode 版命令）语义相同，只是参数可以是任意字符串表达式。

## 用法

### int STRLENSU(str s)

- `s`：要测量长度的字符串表达式。

```erb
A = STRLENSU("abc")       ; A = 3
B = STRLENSU("あいう")     ; B = 3（全角也算 1 个字符）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:114`（`["STRLENSU"] = new StrlenuMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4243`（`private sealed class StrlenuMethod`）

```text
函数 STRLENSU(s):
    返回 s.Length        ; 直接取 .NET 字符串（UTF-16）长度，无任何换算
```

## 备注

- 文档（`ecd/Command.md`）称其为 `STRLENS` 的 Unicode 版、区别是全角字符算 1 个字符，与源码完全一致。
- 本仓库中 `STRLENSU` 仅注册为式中函数；注册为命令的是 `STRLENU`（`Runtime/Script/Statements/FunctionIdentifier.cs:263`）。没有同名的独立命令实现。
