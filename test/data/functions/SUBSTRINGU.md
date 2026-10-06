# SUBSTRINGU

- **类别**：式中函数
- **签名**：str SUBSTRINGU(str s, int start = 0, int length = -1)
- **文档来源**：`ecd/Expression.md`（签名表）、`ecd/Command.md`「### SUBSTRINGU」

## 语义

`SUBSTRING` 的 Unicode 版：从字符串 `s` 中取出子串并返回。`start` 与 `length` 按 Unicode 字符数计算，全角字符算 1。`start` 从 0 开始；`length` 为负（默认 -1）表示取到末尾。`start >= 字符串长度` 或 `length == 0` 时返回空字符串 `""`；`start + length` 超过末尾时截断到末尾。字符边界总是清晰，不存在 `SUBSTRING` 那样"多取 1 个字符"的问题。

也可以写成独立命令形式 `SUBSTRINGU s, start, length`，结果存入 `RESULTS:0`。

## 用法

### str SUBSTRINGU(str s, int start = 0, int length = -1)

- `s`：被截取的字符串表达式。
- `start`：起始字符索引（从 0 起），可省略，默认 0。
- `length`：要取的字符数；负值表示取到末尾，可省略，默认 -1。

```erb
STR = SUBSTRINGU("あいうえお", 2, 2)   ; STR = "いう"
STR = SUBSTRINGU("あいう", 1)          ; STR = "いう"（取到末尾）
STR = SUBSTRINGU("あいう", 5)          ; STR = ""（起点越界）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:116`（`["SUBSTRINGU"] = new SubstringuMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4303`（`private sealed class SubstringuMethod`）

```text
函数 SUBSTRINGU(s, start = 0, length = -1):
    若 start >= s.Length 或 length == 0:
        返回 ""
    若 length < 0 或 length > s.Length:
        length = s.Length                    ; 取到末尾
    若 start <= 0:
        若 length == s.Length: 返回 s        ; 全量返回原串
        start = 0
    若 start + length > s.Length:
        length = s.Length - start            ; 末尾截断
    返回 s.Substring(start, length)          ; 纯 .NET 字符串截取
```

## 备注

- 文档仅说明它是 `SUBSTRING` 的 Unicode 版（全角算 1 个字符），与源码一致。
- 源码比文档多一个细节：`start <= 0 且 length == 字符串总长` 时直接返回原字符串引用（纯优化，不影响语义）。
