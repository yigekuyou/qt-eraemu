# PRINTFORMCK

- **类别**：命令
- **签名**：PRINTFORMCK <FORM格式文本>
- **文档来源**：`ecd/docs/translation/Command.md`（无独立小节，属于「### PRINT(|FORM)(C|LC)(|K|D)」系列说明）；`Era-Chinese-Documentation/docs/Command.md` 之 `### # Print(|Form)(C|LC)(|K|D)`（组合小节，涵盖本命令，无独立条目）

## 语义

PRINTC 系列的 FORM + C + K 组合变体：先把参数当作 FORM 格式文本求值（`{...}`、`%...%` 等展开），再按 PRINTC 单元格宽度（默认 25）**右对齐**输出（左侧补半角空格）。`K` 后缀表示展开后的文本按 `FORCEKANA` 设定做假名转换。输出时应用 `SETCOLOR` 颜色。输出后不换行、不等待输入。受 `SKIPDISP` 影响。

## 用法

### PRINTFORMCK <FORM格式文本>

- `<FORM格式文本>`：FORM 系参数（FORM_STR_NULLABLE），展开后输出。
- 对齐方式：右对齐。
- 假名转换：应用 `FORCEKANA` 设定。

```erb
FORCEKANA 2
NAME:0 = ひらがな
PRINTFORMCK %NAME:0%です
PRINTL
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:156`（`addPrintFunction(FunctionCode.PRINTFORMCK)` → `PRINT_Instruction`，参数构造器 `FORM_STR_NULLABLE`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:299`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:84`（`PRINT_Instruction`，所有 PRINT 系变体共用）

```text
构造期（解析命令名 "PRINTFORMCK"）：
    跳过 "PRINT"
    读到 "FORM" → 参数构造器 = FORM_STR_NULLABLE
    读到 "C" → isC = true
    读到 "K" → flag |= ISPRINTKFUNC | EXTENDED
    不含 "D"、不含 N/L/W → flag |= METHOD_SAFE
    若名称未消耗完 → 抛 ExeEE

执行期 DoInstruction(exm, func, state)：
    若 Process.SkipPrint → 直接返回
    Console.UseUserStyle = true
    Console.UseSetColorStyle = true   ← 非 D 系：应用 SETCOLOR 颜色
    str = 参数 Term.GetStrValue(exm)  ← FORM 展开
    IsPrintKFunction() = true → str = exm.ConvertStringType(str)
        依 FORCEKANA：1=片假名、2=平假名、3=平假名+半角转全角、0=原样
    isC = true → Console.PrintC(str, alignmentRight = true)
        右对齐：左侧补半角空格至 PrintCLength 宽度
    Console.UseSetColorStyle = true（恢复）
```

## 备注

- 两套文档均无 PRINTFORMCK 独立条目，语义依据 ecd 文档「PRINT(|FORM)(C|LC)(|K|D)」系列说明与源码推导。
- 注意假名转换发生在 FORM 展开之后，即转换的是展开结果字符串。
