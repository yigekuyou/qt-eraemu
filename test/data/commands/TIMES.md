# TIMES

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令（Eramaker 基本命令）
- **签名**：TIMES `<数值型变量>`, `<小数常数>`
- **文档来源**：`ecd/docs/translation/ERB_File_Format.md`「### 其他基本命令」（TIMES 作为基本命令列出）；Era-Chinese-Documentation `ERB_File_Format.md`「`TIMES`：支持小数的乘法运算」（内容一致）；两套 Command.md 均无独立小节

## 语义

让变量与小数相乘并把结果存回该变量。EraBasic 的普通赋值/运算对整数处理（小数会被取整），`TIMES` 用于支持小数的乘法运算，例如 `TIMES MONEY , 1.25` 把 `MONEY` 乘以 1.25 后存回 `MONEY`。结果按截断取整存回整数变量。

计算精度受配置项 `TIMESの計算をeramakerにあわせる`（`TIMES 的计算是否与 Eramaker 一致`，对应实现中的 `Config.TimesNotRigorousCalculation`）影响：开启时用 double 直接运算（与 Eramaker 旧精度一致），关闭时用 decimal 高精度运算后取整。

## 用法

### TIMES `<数值型变量>`, `<小数常数>`
- 第 1 参数：被乘的数值型变量（必须是可以赋值的变量，不能是常量或字符串变量）。
- 第 2 参数：乘数，必须是小数字面常数（解析期直接读成 double，不接受表达式）。

```erb
MONEY = 500
TIMES MONEY , 1.25
PRINTFORML 我有{MONEY}元钱   ;→ 625
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:191`（`new TIMES_Instruction()`，flag = METHOD_SAFE；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:59`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1363`（`TIMES_Instruction`）；参数构造 `Runtime/Script/Statements/ArgumentBuilder.cs:520`（`SP_TIMES_ArgumentBuilder`）；参数存放 `Runtime/Script/Statements/Argument.cs:143`（`SpTimesArgument`：变量项 + double 值）

```text
参数解析(SP_TIMES):
  先读第 1 参数的词法 → 解析为表达式 → 必须 Restructure 后是 VariableTerm
      非 VariableTerm        → 警告“第 1 参数不是变量”
      是字符串型变量          → 警告“第 1 参数应为数值变量”
      是常量（IsConst 标识符）→ 警告“第 1 参数不能是常量”
  再读第 2 参数 → LexicalAnalyzer.ReadDouble 直接读 double 字面量
      读不出来 → 警告“第 2 参数不是实数”，按 0.0 处理
  之后还有内容 → 警告“参数过多”
  生成 SpTimesArgument(变量项, double 乘数)

DoInstruction:
  var = timesArg.VariableDest
  若 Config.TimesNotRigorousCalculation:      // 与 eramaker 精度一致
      d = var.GetIntValue(exm) * timesArg.DoubleValue     // double 运算
      var.SetValue((long)d)                                // 截断取整存回
  否则:                                        // 高精度
      d = (decimal)var.GetIntValue(exm) * (decimal)timesArg.DoubleValue
      若 d 在 long 范围内: var.SetValue((long)d)
      否则:               var.SetValue((long)(double)d)  // 溢出时退回 double 精度
```

## 备注

- 两套文档都只把 TIMES 当作「其他基本命令」一句话带过，签名 `<变量>,<小数常数>` 来自源码（`Runtime/Script/Statements/FunctionArgType.cs:22` 注释 `SP_TIMES,//<数値型変数>,<実数定数>`）。
- 第 2 参数在解析期必须是字面常数（`ReadDouble`），不能写 `TIMES X, Y*2` 之类的表达式；文档未提及此限制。
- 源码注释说明：decimal 运算会强制抛 OverflowException，因此溢出时改走 double 以「接近旧行为」。
- 本仓库中 TIMES 是普通指令而非运算符；`.erb` 脚本中 `A = B TIMES 1.25` 的写法（zh 文档示例风格）以 `TIMES 目标变量 , 小数` 的语句形式为准。
