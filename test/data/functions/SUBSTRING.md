# SUBSTRING

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数
- **签名**：str SUBSTRING(str s, int start = 0, int length = -1)
- **文档来源**：`ecd/Expression.md`（签名表 + 教程省略参数示例）、`ecd/Command.md`「### SUBSTRING」

## 语义

从字符串 `s` 中取出子串并返回。`start` 与 `length` 按 Shift-JIS（当前语言编码）**字节数**指定，全角字符算 2。`start` 从 0 开始；`length` 为负（默认 -1）表示取到字符串末尾。起始位置超过字符串长度、或 `length` 为 0 时返回空字符串 `""`。

切分点落在全角字符中间时，实现会向后对齐到下一个字符边界，因此实际取到的串可能比指定的 `length` 多出约 1 个字符（文档明确提醒了这一点）。

也可以写成独立命令形式 `SUBSTRING s, start, length`，结果存入 `RESULTS:0`。

## 用法

### str SUBSTRING(str s, int start = 0, int length = -1)

- `s`：被截取的字符串表达式。
- `start`：起始位置（字节索引，从 0 起），可省略，默认 0。
- `length`：要取的字节数；负值表示取到末尾，可省略，默认 -1。

```erb
STR = SUBSTRING(RESULTS)          ; 整个 RESULTS
STR = SUBSTRING(RESULTS, 0)       ; 同上（start=0，length=-1）
STR = SUBSTRING(RESULTS, , -1)    ; 同上（省略 start 保留 length 位置）
STR = SUBSTRING(RESULTS, 0, -1)   ; 同上
LOCALS:0 = %SUBSTRING(STR:0, A, 1)%   ; 取 STR:0 从 A 字节起的 1 个"字符"
SUBSTRING "あいう", 2, 2           ; 命令形式：RESULTS:0 = "い"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:115`（`["SUBSTRING"] = new SubstringMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4258`（`private sealed class SubstringMethod`），实际截取在 `Runtime/Utils/LangManager.cs:71`（`GetSubStringLang`）

```text
函数 SUBSTRING(s, start = 0, length = -1):
    返回 LangManager.GetSubStringLang(s, start, length)

LangManager.GetSubStringLang(str, startindex, length):
    totalByte = GetStrlenLang(str)               ; 与 STRLENS 相同的字节计数
    若 startindex >= totalByte 或 length == 0:
        返回 ""
    若 length < 0 或 length > totalByte:
        length = totalByte                       ; 取到末尾
    UTFcnt = 0 ; JIScnt = 0
    若 startindex <= 0:
        若 length == totalByte: 返回 str          ; 全量拷贝
    否则:
        循环: 逐字符累加字节数到 JIScnt，UTFcnt++
              直到 JIScnt >= startindex          ; 起点向后对齐到字符边界
        若 UTFcnt >= str.Length: 返回 ""
    JIScnt = 0
    循环:
        ret += str[UTFcnt]                       ; 先追加再判断，
        JIScnt += 字节数(str[UTFcnt])             ; 因此边界上会多取 1 个字符
        UTFcnt++
        若 JIScnt >= length 或 UTFcnt >= str.Length: 退出
    返回 ret
```

## 备注

- 文档与源码一致：「起始位置超过长度返回空串」（`startindex >= totalByte`）、「长度为负或超过末尾时取到末尾」（`length < 0 → totalByte`）、「可能在边界多取 1 个字符」（实现为先追加后判断）。
- 文档中的"起始位置或结束位置……被判断为向后偏移 1 个位置"对应实现中起点跳过整字符、终点多取一个字符的行为。
- 本仓库中无同名命令注册；命令形态是式中函数的独立调用写法，结果进 `RESULTS:0`（与 `ecd/Command.md` 的描述吻合）。
