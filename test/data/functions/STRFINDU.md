# STRFINDU

- **类别**：式中函数
- **签名**：int STRFINDU(str str, str find, int start = 0)
- **文档来源**：`ecd/Expression.md`（签名表）、`ecd/Command.md`「### STRFINDU」

## 语义

`STRFIND` 的 Unicode 版：在字符串 `str` 中从 `start` 位置起查找子串 `find`，返回其出现位置；未找到返回 `-1`。起始索引与返回值都以 Unicode 字符计数，全角字符算 1，不做字节换算。

也可以写成独立命令形式 `STRFINDU str, find(, start)`，结果存入 `RESULT:0`。

## 用法

### int STRFINDU(str str, str find, int start = 0)

- `str`：被查找的字符串表达式。
- `find`：要查找的字符串表达式。
- `start`：查找起始字符索引（从 0 起），可省略，默认 0。

```erb
A = STRFINDU("あいう", "い")       ; A = 1（Unicode 计数）
B = STRFINDU("abcabc", "abc", 1)  ; B = 3
C = STRFINDU("あいう", "え")       ; C = -1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:118`（`["STRFINDU"] = new StrfindMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4361`（`private sealed class StrfindMethod`，构造参数 `unicode=true`）

```text
函数 STRFINDU(target, word, start = 0):     ; unicode = true
    UFTstart = start                        ; 直接使用，不做字节换算
    若 UFTstart < 0 或 UFTstart >= target.Length:
        返回 -1
    index = target.IndexOf(word, UFTstart, Ordinal)
    返回 index                              ; 已是 Unicode 索引，无需换算
```

## 备注

- 与 `STRFIND` 共用 `StrfindMethod` 类，仅构造参数 `unicode` 不同；`unicode=true` 时跳过 `LangManager.GetUFTIndex` 的入口换算和结果的字节索引回转。
- 文档（`ecd/Command.md`）描述「返回值的字符位置与起始索引都以 Unicode 计数」，与源码一致，无差异。
