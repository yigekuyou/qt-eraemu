# PRINTLCK

- **类别**：命令
- **签名**：PRINTLCK <文本>
- **文档来源**：`ecd/docs/translation/Command.md`（无独立小节，属于「### PRINT(|FORM)(C|LC)(|K|D)」系列说明）；`Era-Chinese-Documentation/docs/Command.md` 之 `### # Print(|Form)(C|LC)(|K|D)`（组合小节，涵盖本命令，无独立条目）

## 语义

PRINTC 系列的 LC + K 组合变体：把文本按 PRINTC 单元格宽度（配置「PRINTC 文字长度」，默认 25）**左对齐**输出（右侧补半角空格）。`K` 后缀表示输出前按 `FORCEKANA` 指令设定的模式对文本做平假名/片假名（及半角→全角）转换；若未用 FORCEKANA 设定（或设定为 0），文本原样输出。输出时应用 `SETCOLOR` 颜色。输出后不换行、不等待输入。受 `SKIPDISP` 影响。

## 用法

### PRINTLCK <文本>

- `<文本>`：可省略的字符串（STR_NULLABLE，不需要引号）。
- 对齐方式：左对齐（与 PRINTLC 相同）。
- 假名转换：应用 `FORCEKANA` 设定（与 PRINTK 系列相同）。

```erb
FORCEKANA 1
PRINTLCK こ、こんにちは
PRINTL
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:155`（`addPrintFunction(FunctionCode.PRINTLCK)` → `PRINT_Instruction`，参数构造器 `STR_NULLABLE`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:298`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:84`（`PRINT_Instruction`，所有 PRINT 系变体共用）

```text
构造期（解析命令名 "PRINTLCK"）：
    跳过 "PRINT"；无 V/S/FORM 前缀 → 参数构造器 = STR_NULLABLE
    读到 "LC" → isLC = true；flag |= EXTENDED
    读到 "K" → flag |= ISPRINTKFUNC | EXTENDED
    不含 "D"、不含 N/L/W → flag |= METHOD_SAFE
    若名称未消耗完 → 抛 ExeEE

执行期 DoInstruction(exm, func, state)：
    若 Process.SkipPrint → 直接返回
    Console.UseUserStyle = true
    Console.UseSetColorStyle = !IsPrintDFunction() = true   ← 非 D 系：应用 SETCOLOR 颜色
    str = 参数文本（本命令无 FORM 展开）
    IsPrintKFunction() = true → str = exm.ConvertStringType(str)
        ConvertStringType：依 FORCEKANA 的设定值
            1 → 转为片假名（Katakana）
            2 → 转为平假名（Hiragana）
            3 → 转为平假名并半角转全角（Hiragana | Wide）
            0/未设定 → 原样返回
    isLC = true → Console.PrintC(str, alignmentRight = false)
        左对齐：右侧补半角空格至 PrintCLength + 1 个字节宽
    Console.UseSetColorStyle = true（恢复）
```

## 备注

- 两套文档均无 PRINTLCK 独立条目，语义依据 ecd 文档「PRINT(|FORM)(C|LC)(|K|D)」系列说明与源码推导。
- `FORCEKANA` 参数越界（<0 或 >3）时会在设置处抛 CodeEE；K 后缀本身对未设定 FORCEKANA 的文本无影响。
- 文档注明：从 1.736 版本开始 `K` 与 `D` 不能同时指定（本命令只含 K，不涉及冲突）。
