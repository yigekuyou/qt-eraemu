# CHARATU

- **类别**：式中函数
- **签名**：`str CHARATU(str s, int position)`（ecd 文档签名写作 `str CHARATU(str s, int position = 0)`）
- **文档来源**：`ecd/Expression.md`（字符串处理函数签名列表）、`ecd/Command.md`「CHARATU `<字符串表达式>`, `<文字位置>`」小节；zh 套件未收录

## 语义

取出字符串 `s` 中第 `position` 个字符（以单个字符为单位，Unicode 计数，从 0 开始），以字符串形式返回。是 `SUBSTRINGU` 的取单字符特例版，处理体系为 Unicode，全角/半角都算 1 个字符。

`position` 超出字符串范围（负值或 ≥ 字符串长度）时不报错，返回空字符串。返回值是字符串，须用 `RESULTS` 或字符串表达式接住。

## 用法

### str CHARATU(str s, int position)
- `s`：被取字符的字符串表达式。
- `position`：字符位置（从 0 起）。越界返回空串。
```erb
STR:0 = "abcあいう"
PRINTFORML CHARATU(STR:0, 0)   ; → a
PRINTFORML CHARATU(STR:0, 3)   ; → あ（Unicode 计数，全角算 1 字符）
PRINTFORML CHARATU(STR:0, 99)  ; → （空字符串）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:134`（`["CHARATU"] = new CharAtMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4851`（`CharAtMethod`，返回类型 string，参数 `[string, long]`，`CanRestructure = true`）

```text
函数 CHARATU(s, pos):
    str ← s 求值（字符串）
    pos ← pos 求值（整数）
    若 pos < 0 或 pos ≥ str.Length（.NET 字符串长度，按 UTF-16 码元计）:
        返回 ""
    返回 str[pos] 的单字符字符串
```

## 备注

- ecd 文档签名把 `position` 写成默认值 `= 0`，但源码 `argumentTypeArray = [string, long]` 未开放省略，两个参数都必须给出；实际使用时应按必填对待。
- `.NET 的 str.Length` 按 UTF-16 码元计数，对 BMP 内字符与 Unicode 字符数一致；源码直接以 `str.Length` 做边界判断、`str[pos]` 取字符，日常全角/汉字场景等同按字符取。
- 同类函数：`SUBSTRINGU`（按字符取子串）、`ENCODETOUNI`（取字符的 Unicode 码位）、`TOFULL/TOHALF`。
- zh 套件未收录该函数。
