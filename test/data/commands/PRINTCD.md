# PRINTCD

- **类别**：命令
- **签名**：`PRINTCD <字符串表达式>`（右对齐，忽略 SETCOLOR；另有 `PRINTLCD` / `PRINTFORMCD` / `PRINTFORMLCD` 变体，不在本文件范围）
- **文档来源**：`ecd/docs/translation/Command.md` 之 `### PRINT(|FORM)(C|LC)(|K|D)` 小节（PRINTCD 本身无独立小节，由该节的关键字组合规则涵盖）；`Era-Chinese-Documentation/docs/Command.md` 之 `### # Print(|Form)(C|LC)(|K|D)`

## 语义

PRINTCD 是 `PRINTC` 系列中带 `D` 关键字的变体。它把字符串参数以**右对齐**方式绘制：宽度不足「PRINTC 文字长度」设置（默认 25）时左侧用半角空格补齐。`D` 关键字表示**忽略 `SETCOLOR` 指令设置的颜色，改用设置文件指定的默认颜色**绘制。

它与 `PRINTC` 的区别仅在于 D 标志：绘制行为（右对齐、补空格）完全一致。

## 用法

### PRINTCD <字符串表达式>
按右对齐绘制字符串表达式求值结果，宽度按「PRINTC 文字长度」设置补半角空格；绘制时忽略 `SETCOLOR`，使用默认颜色。

```erb
SETCOLOR 0xFF0000
PRINTCD "这段文字仍显示默认颜色"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:158`（`addPrintFunction(FunctionCode.PRINTCD)` → `new PRINT_Instruction("PRINTCD")`）；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:332`（源码注释为 `//??`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:84`（类 `PRINT_Instruction`，构造解析函数名；`DoInstruction` 在 180-222 行）

```text
构造 PRINT_Instruction("PRINTCD") 时解析名字:
    跳过前缀 "PRINT"
    剩余 "CD": 不是 V/S/FORMS/FORM → 参数类型 = STR_NULLABLE（字符串表达式，可空）
    匹配 "LC"? 否；匹配 "C"? 是 → isC = true（右对齐），跳过 'C'
    匹配 "K"? 否
    匹配 "D"? 是 → flag |= ISPRINTDFUNC | EXTENDED（忽略 SETCOLOR 的标记），跳过 'D'
    匹配 "L"/"W"? 否 → flag |= METHOD_SAFE
    （名字必须恰好解析完，否则抛 ExeEE）

执行 DoInstruction:
    若全局 SkipPrint（SKIPDISP 生效）→ 直接返回，不绘制
    Console.UseUserStyle = true
    Console.UseSetColorStyle = !IsPrintDFunction()   # 有 D → false：不应用 SETCOLOR，用默认颜色
    str = 字符串参数求值
    若 ISPRINTKFUNC: str = ConvertStringType(str)     # PRINTCD 无 K，不转换
    isC → Console.PrintC(str, true)                   # true = 右对齐（左侧补半角空格）
    Console.UseSetColorStyle = true
```

## 备注

- 两套文档都没有 PRINTCD 的独立小节；ecd 的 `### PRINT(|FORM)(C|LC)(|K|D)` 与 zh 的对应小节以关键字组合方式涵盖它。
- 源码枚举中 PRINTCD 标注 `//??`，未写注释，但实现路径与 PRINTC 一致，仅多了 D 标志（UseSetColorStyle = false）。
- 「PRINTC 文字长度」由配置项 `Config.PrintCLength`（配置键「PRINTCの文字数」、中文界面显示为「PRINTC项目的文字长度」，默认 25）控制，表达式中可用同名函数 `PRINTCLENGTH()` 读取；与 `PRINTCPERLINE`（每行并列个数）是两个不同设置。
