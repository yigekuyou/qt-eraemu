# PRINTCK

- **类别**：命令
- **签名**：`PRINTCK <字符串表达式>`（右对齐，应用 FORCEKANA；另有 `PRINTLCK` / `PRINTFORMCK` / `PRINTFORMLCK` 变体，不在本文件范围）
- **文档来源**：`ecd/docs/translation/Command.md` 之 `### PRINT(|FORM)(C|LC)(|K|D)` 小节（PRINTCK 本身无独立小节，由该节的关键字组合规则涵盖）；`Era-Chinese-Documentation/docs/Command.md` 之 `### # Print(|Form)(C|LC)(|K|D)`

## 语义

PRINTCK 是 `PRINTC` 系列中带 `K` 关键字的变体。它把字符串参数以**右对齐**方式绘制：若文本宽度不足 Emuera 设置中「PRINTC 文字长度」（默认 25）指定的宽度，则左侧用半角空格补齐。`K` 关键字表示应用 `FORCEKANA` 指令的假名转换设置（平假名↔片假名等），且按源码它还带有 EXTENDED（扩展）标记。

它与 `PRINTC` 的区别仅在于 K 标志：绘制行为（右对齐、补空格、按钮化处理的特殊性）完全一致。

## 用法

### PRINTCK <字符串表达式>
按右对齐绘制字符串表达式求值结果，宽度按「PRINTC 文字长度」设置补半角空格；绘制时应用 `FORCEKANA` 设置。

```erb
PRINTCK "右对齐文本"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:154`（`addPrintFunction(FunctionCode.PRINTCK)` → `new PRINT_Instruction("PRINTCK")`）；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:297`（源码注释为 `//??`，即原作者未特别注明）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:84`（类 `PRINT_Instruction`，构造解析函数名；`DoInstruction` 在 180-222 行）

```text
构造 PRINT_Instruction("PRINTCK") 时解析名字:
    跳过前缀 "PRINT"
    剩余 "CK": 不是 V/S/FORMS/FORM → 参数类型 = STR_NULLABLE（字符串表达式，可空）
    匹配 "LC"? 否；匹配 "C"? 是 → isC = true（右对齐），跳过 'C'
    匹配 "K"? 是 → flag |= ISPRINTKFUNC | EXTENDED（应用 FORCEKANA 的标记），跳过 'K'
    匹配 "D"? 否
    匹配 "L"/"W"? 否 → flag |= METHOD_SAFE
    （名字必须恰好解析完，否则抛 ExeEE）

执行 DoInstruction:
    若全局 SkipPrint（SKIPDISP 生效）→ 直接返回，不绘制
    Console.UseUserStyle = true
    Console.UseSetColorStyle = true          # 无 D 关键字，应用 SETCOLOR 颜色
    str = 字符串参数求值
    若 ISPRINTKFUNC: str = exm.ConvertStringType(str)   # 依 FORCEKANA 设置做平/片假名转换
    isC → Console.PrintC(str, true)          # true = 右对齐（左侧补半角空格）
    Console.UseSetColorStyle = true
```

## 备注

- 两套文档都没有 PRINTCK 的独立小节；ecd 的 `### PRINT(|FORM)(C|LC)(|K|D)` 与 zh 的 `### # Print(|Form)(C|LC)(|K|D)` 以关键字组合方式涵盖它。
- 源码枚举中 PRINTCK 标注 `//??`，未写注释，但不影响实现：它走与 PRINTC 完全相同的 `PRINT_Instruction` 路径。
- 文档称 K/D 「1.736 版本开始不能同时指定」；本仓库解析器按名字顺序依次匹配 K、D，两者在 `PRINTCKD` 这种名字上并不会被注册（无该枚举值），与文档一致。
