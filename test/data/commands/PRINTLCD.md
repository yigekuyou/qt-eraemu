# PRINTLCD

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令
- **签名**：PRINTLCD <文本>
- **文档来源**：`ecd/docs/translation/Command.md`（无独立小节，属于「### PRINT(|FORM)(C|LC)(|K|D)」系列说明）；`Era-Chinese-Documentation/docs/Command.md` 之 `### # Print(|Form)(C|LC)(|K|D)`（组合小节，涵盖本命令，无独立条目）

## 语义

PRINTC 系列的 LC + D 组合变体：把文本按 PRINTC 的单元格宽度（配置「PRINTC 文字长度」，默认 25）**左对齐**输出（右侧补半角空格）。`D` 后缀表示输出时**忽略 `SETCOLOR` 设置的颜色**，改用配置文件指定的默认文字颜色（字体、字号等仍沿用用户样式）。输出后不换行、不等待输入。受 `SKIPDISP` 影响：跳过显示时不输出。

## 用法

### PRINTLCD <文本>

- `<文本>`：可省略的字符串（STR_NULLABLE，不需要引号）。为空时该单元格不占位。
- 对齐方式：左对齐（与 PRINTLC 相同）。
- 颜色：忽略 `SETCOLOR`，使用默认颜色（与 PRINTD 系列相同）。

```erb
SETCOLOR 0xFF0000
PRINTLCD 红色设置下也按默认颜色显示
PRINTL
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:159`（`addPrintFunction(FunctionCode.PRINTLCD)`，即注册为 `PRINT_Instruction`，参数构造器 `STR_NULLABLE`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:333`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:84`（`PRINT_Instruction`，私有类，所有 PRINT 系变体共用）

```text
构造期（解析命令名 "PRINTLCD"）：
    跳过前 5 个字符 "PRINT"
    无 V/S/FORM 前缀 → 参数构造器 = STR_NULLABLE（裸文本参数）
    读到 "LC" → isLC = true；flag |= EXTENDED
    不含 "K"
    读到 "D" → flag |= ISPRINTDFUNC（PRINTD 系标志）
    不含 N/L/W → flag |= METHOD_SAFE（安全指令，可在任何上下文使用）
    若名称未消耗完 → 抛 ExeEE（非法 PRINT 命令）

执行期 DoInstruction(exm, func, state)：
    若 GlobalStatic.Process.SkipPrint（SKIPDISP 1 且不在 NOSKIP 区间）→ 直接返回
    Console.UseUserStyle = true（应用用户样式）
    Console.UseSetColorStyle = !IsPrintDFunction() = false   ← D 后缀：不用 SETCOLOR 颜色
    str = 参数文本（常量直接取，否则求值；本命令无 FORM 展开）
    不是 K 系 → 不做 FORCEKANA 平假名/片假名转换
    isLC = true → Console.PrintC(str, alignmentRight = false)
        PrintC 内部：按 Shift-JIS 字节数计算 str 宽度，
        在右侧补足到 (PrintCLength + 1) 个字节宽（即追加 PrintCLength + 1 - 长度 个半角空格），
        若补齐后显示宽度超出目标宽度则从右端逐个删空格，追加到输出缓冲
    Console.UseSetColorStyle = true（恢复）
```

## 备注

- 两套文档均无 PRINTLCD 独立条目，语义依据 ecd 文档中「PRINT(|FORM)(C|LC)(|K|D)」系列说明与源码推导。
- 源码中 D 后缀的实际效果（`UseSetColorStyle = false`，见 `UI/Game/EmueraConsole.Print.cs:83-86`）：颜色用默认色，但字体名/字形仍取用户样式；这与文档「使用设置文件指定的默认颜色」一致。
- PRINTLCD 并非 emuera.em 独有：本仓库另一棵 Emuera 本家源码树中也存在（`$R/Emuera/GameProc/Function/BuiltInFunctionCode.cs:332` 枚举、`$R/Emuera/GameProc/Function/FunctionIdentifier.cs:155` 注册；`$R` = 仓库根），并收录于 `test/data/emuera_standard_cmds.txt`（原版命令清单）。
