# TOLOWER

- **类别**：式中函数
- **签名**：
  - `str TOLOWER(str value)`
- **文档来源**：`ecd/docs/translation/Command.md`「### TOLOWER `<字符串表达式>`」、`ecd/Expression.md` 函数目录（`str TOLOWER(str value)`）；zh 套件未收录该函数小节

## 语义

把参数字符串中的字母转换为小写后返回。空字符串或 null 返回空字符串。与指令版 `TOLOWER`（结果写入 `RESULTS:0`）不同，本形态在表达式中直接返回转换结果。

## 用法

### `str TOLOWER(str value)`
```erb
STR:0 = TOLOWER("AbC")          ; STR:0 = "abc"
PRINTFORML {TOLOWER("ABC")}     ; 输出 abc
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:123`（`["TOLOWER"] = new StrChangeStyleMethod(StrFormType.Lower)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4547`（`StrChangeStyleMethod`，共用类，由构造参数 `StrFormType.Lower` 决定行为；`StrFormType` 枚举在同文件 `:4539`）

```text
函数 TOLOWER(s):
    若 s 为 null 或空串: 返回 ""
    按 strType == Lower 分支: 返回 s.ToLower()    # .NET 默认区域文化的大小写转换
```

## 备注

- `StrChangeStyleMethod` 是 TOUPPER / TOLOWER / TOHALF / TOFULL 四个函数共用的实现类，仅构造时的 `StrFormType` 不同（Upper=0, Lower=1, Half=2, Full=3）；TOUPPER 与 TOLOWER 走 `string.ToUpper()/ToLower()`，TOHALF/TOFULL 则转调 VB 的 `StrConv`（本函数不涉及）。
- 文档将结果描述为「赋值给 `RESULTS:0`」，那是同名指令形态；式中函数形态直接返回值，二者均存在。
- `CanRestructure = true`：常量参数在解析期折叠。
- ecd 文档未说明转换按何种区域文化进行；源码用 .NET 默认文化的 `ToLower()`。
