# STRFIND

- **类别**：式中函数
- **签名**：int STRFIND(str str, str find, int start = 0)
- **文档来源**：`ecd/Expression.md`（签名表）、`ecd/Command.md`「### STRFIND」

## 语义

字符串查找：在字符串 `str` 中从 `start` 位置起查找子串 `find`，返回其出现位置；未找到返回 `-1`。

`start` 与返回值的位置索引都按 Shift-JIS（当前语言编码）**字节数**计（全角字符算 2），从 0 开始。内部查找用 Unicode 的 Ordinal 比较完成，字节索引只用于入口换算与结果换算。第 3 参数 `start` 自 Emuera 1.712 起可用。

也可以写成独立命令形式 `STRFIND str, find(, start)`，结果存入 `RESULT:0`。

## 用法

### int STRFIND(str str, str find, int start = 0)

- `str`：被查找的字符串表达式。
- `find`：要查找的字符串表达式。
- `start`：查找起始位置（字节索引，从 0 起），可省略，默认 0。

```erb
A = STRFIND("あいう", "い")        ; A = 2（"あ"占 2 字节）
B = STRFIND("abcabc", "abc", 1)   ; B = 3（从字节位置 1 起找）
C = STRFIND("abc", "xyz")         ; C = -1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:117`（`["STRFIND"] = new StrfindMethod(false)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4361`（`private sealed class StrfindMethod`，构造参数 `unicode=false`），索引换算在 `Runtime/Utils/LangManager.cs:52`（`GetUFTIndex`）

```text
函数 STRFIND(target, word, start = 0):      ; unicode = false
    UFTstart = LangManager.GetUFTIndex(target, start)   ; 字节索引 → UTF 字符索引
    若 UFTstart < 0 或 UFTstart >= target.Length:
        返回 -1
    index = target.IndexOf(word, UFTstart, Ordinal)     ; 内部按 Unicode 查找
    若 index > 0:
        index = LangManager.GetStrlenLang(target.Substring(0, index))
                                                            ; 前缀转回字节索引
    返回 index          ; index == 0 时保持 0；未找到为 -1

LangManager.GetUFTIndex(str, LangIndex):    ; 把字节索引换算成字符索引
    若 LangIndex <= 0: 返回 0
    若 LangIndex >= 总字节数: 返回 str.Length
    逐字符累加字节数，直到累计 >= LangIndex，返回此时的字符数（向上取整到字符边界）
```

## 备注

- 文档说返回"不区分全角/半角、按全角字符算 2 个字符、从 0 开始的索引"，即字节索引，与源码一致；源码细节：`start` 先被向上取整到字符边界（`GetUFTIndex`），结果索引由前缀字节计数得到。
- 与 `STRFINDU` 共用同一个类，仅构造参数不同；真正的区别见 STRFINDU 文档。
- `ecd/Command.md` 还提到 1.712 起才支持第 3 参数，与源码中 `OmitStart = 2`（第 3 参数可省略）相符。
