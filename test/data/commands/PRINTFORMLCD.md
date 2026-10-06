# PRINTFORMLCD

- **类别**：命令
- **签名**：PRINTFORMLCD <FORM格式文本>
- **文档来源**：`ecd/docs/translation/Command.md`（无独立小节，属于「### PRINT(|FORM)(C|LC)(|K|D)」系列说明）；`Era-Chinese-Documentation/docs/Command.md` 之 `### # Print(|Form)(C|LC)(|K|D)`（组合小节，涵盖本命令，无独立条目）

## 语义

PRINTC 系列的 FORM + LC + D 组合变体：先把参数当作 FORM 格式文本求值（`{...}`、`%...%` 等展开），再按 PRINTC 单元格宽度（默认 25）**左对齐**输出（右侧补半角空格）。`D` 后缀表示忽略 `SETCOLOR` 颜色，使用配置默认色。输出后不换行、不等待输入。受 `SKIPDISP` 影响。

## 用法

### PRINTFORMLCD <FORM格式文本>

- `<FORM格式文本>`：FORM 系参数（FORM_STR_NULLABLE），展开后输出。
- 对齐方式：左对齐（与 PRINTLC 相同）。
- 颜色：忽略 `SETCOLOR`，使用默认颜色。

```erb
A = 5
PRINTFORMLCD 第{A}项
PRINTL
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:161`（`addPrintFunction(FunctionCode.PRINTFORMLCD)` → `PRINT_Instruction`，参数构造器 `FORM_STR_NULLABLE`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:335`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:84`（`PRINT_Instruction`，所有 PRINT 系变体共用）

```text
构造期（解析命令名 "PRINTFORMLCD"）：
    跳过 "PRINT"
    读到 "FORM" → 参数构造器 = FORM_STR_NULLABLE
    读到 "LC" → isLC = true；flag |= EXTENDED
    读到 "D" → flag |= ISPRINTDFUNC
    不含 "K"、不含 N/L/W → flag |= METHOD_SAFE
    若名称未消耗完 → 抛 ExeEE

执行期 DoInstruction(exm, func, state)：
    若 Process.SkipPrint → 直接返回
    Console.UseUserStyle = true
    Console.UseSetColorStyle = false   ← D 后缀：不用 SETCOLOR 颜色
    str = 参数 Term.GetStrValue(exm)   ← FORM 展开
    非 K 系 → 不做假名转换
    isLC = true → Console.PrintC(str, alignmentRight = false)
        左对齐：右侧补半角空格至 PrintCLength + 1 个字节宽
    Console.UseSetColorStyle = true（恢复）
```

## 备注

- 两套文档均无 PRINTFORMLCD 独立条目，语义依据 ecd 文档「PRINT(|FORM)(C|LC)(|K|D)」系列说明与源码推导。
- 这是 C 系（PRINTC 系列）中最长的变体名，四个后缀成分（FORM / L / D / C）全部启用。
