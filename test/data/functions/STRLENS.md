# STRLENS

- **类别**：式中函数
- **签名**：int STRLENS(str s)
- **文档来源**：`ecd/Expression.md`（签名表 + 式中函数教程示例）、`ecd/Command.md`「### STRLENS」

## 语义

测量字符串 `s` 的长度并返回（int）。长度按当前语言编码（默认 Shift-JIS/CP932）的**字节数**计算，全角字符算 2、半角字符算 1；若字符串纯由 ASCII 组成则直接等于字符数。`STRLEN`（命令）的字符串表达式版，与命令版 `STRLEN` 的区别仅在于参数可以是任意字符串表达式。

与几乎所有式中函数一样，也可以写成独立命令形式 `STRLENS <表达式>`，此时返回值存入 `RESULT:0`。

## 用法

### int STRLENS(str s)

- `s`：要测量长度的字符串表达式。

```erb
A = STRLENS("abc")        ; A = 3
B = STRLENS("あいう")      ; B = 6（全角按 2 字节计）
IF STRLENS(STR:0) > A
	LOCALS:0 = %SUBSTRING(STR:0, A, 1)%
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:113`（`["STRLENS"] = new StrlenMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4228`（`private sealed class StrlenMethod`），实际计数在 `Runtime/Utils/LangManager.cs:44`（`GetStrlenLang`）

```text
函数 STRLENS(s):
    返回 LangManager.GetStrlenLang(s)

LangManager.GetStrlenLang(str):
    若 str 全部为 ASCII 字符:
        返回 str.Length            ; 半角串直接返回字符数
    返回 GetByteCountLang(str)

LangManager.GetByteCountLang(str):
    total = 0
    对 str 的每个字符 c:
        bytes = 当前语言编码.GetBytes(c)
        若 bytes 能无损还原为 c:            ; 该字符在本语言编码中存在
            total += bytes.Length
        否则用日文编码(CP932)重试:
            若能无损还原: total += 日文字节数
            否则:         total += 当前编码字节数
    返回 total
```

## 备注

- `ecd/Command.md` 将本函数以命令形式记载（「以 Shift-JIS 的字节数测量字符串表达式的长度并赋值给 `RESULT:0`」），而本仓库源码中它只注册为式中函数（`Runtime/Script/Statements/Function/Creator.cs`），命令形式是解析器对式中函数独立调用的通用支持；两种写法效果一致。
- 同名命令家族中真正注册为命令的是 `STRLEN`/`STRLENFORM`/`STRLENU`/`STRLENFORMU`（`Runtime/Script/Statements/FunctionIdentifier.cs:261-264`），`STRLENS` 本身没有同名命令。
- 无编码不可映射字符时回退用 CP932 计数，与文档「Shift-JIS 字节数」的说法在极端字符上可能有细微出入（文档未提及该回退）。
