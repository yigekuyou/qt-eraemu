# PRINTCPERLINE

- **类别**：命令（EXTENDED；另有同名式中函数形式）
- **签名**：`PRINTCPERLINE <数值变量>`
- **文档来源**：`ecd/docs/translation/Command.md` 之 `### PRINTCPERLINE`；`Era-Chinese-Documentation/docs/` 未收录（grep 全套 zh 文档无 PERLINE 字样）

## 语义

把 Emuera 设置中「PRINTC 并列数量」（即一行内允许并列排布多少个 `PRINTC` 系指令，超过后会自动折行）读取出来，写入指定的数值变量（惯例写入 `RESULT:0`）。该设置的默认值为 3（源码 `Runtime/Config/ConfigData.cs:64` 中 `ConfigItem<int>(..., 3)`）。

本命令是 EE 扩展：它同时存在同名式中函数形式 `PRINTCPERLINE()`，在表达式中直接返回该设置值。

## 用法

### PRINTCPERLINE <数值变量>
将当前「PRINTC 并列数量」设置值写入 `<数值变量>`。

```erb
PRINTCPERLINE RESULT:0
PRINTFORML 一行可并列 {RESULT:0} 个 PRINTC
```

### （式中函数形式）PRINTCPERLINE()
无参数，返回「PRINTC 并列数量」设置值。（注册为式中函数，供表达式使用。）

```erb
PRINTFORML 一行可并列 {PRINTCPERLINE()} 个 PRINTC
```

## 源码实现（emuera.em/Emuera）

- 注册（命令形式）：`Runtime/Script/Statements/FunctionIdentifier.cs:402`（`addFunction(FunctionCode.PRINTCPERLINE, argb[FunctionArgType.SP_GETINT], METHOD_SAFE | EXTENDED)`，源码注释「よく考えたら引数の仕様違うや」——即与普通式中函数参数规格不同，故未 METHOD 化）；switch 分发 `Runtime/Script/Process.ScriptProc.cs:554`
- 注册（式中函数形式）：`Runtime/Script/Statements/Function/Creator.cs:65`（`["PRINTCPERLINE"] = new GetPrintCPerLineMethod()`）
- 实现（命令形式）：`Runtime/Script/Process.ScriptProc.cs:554-559`；实现（式中函数形式）：`Runtime/Script/Statements/Function/Creator.Method.cs:2841`（类 `GetPrintCPerLineMethod`）

```text
# 命令形式（Process.ScriptProc 的 switch-case）
case PRINTCPERLINE:
    参数由 SP_GETINT 解析为 SpGetIntArgument（一个可赋值的变量引用）
    VarToken.SetValue(Config.PrintCPerLine)   # 把静态配置项 PrintCPerLine 的值写入目标变量

# 式中函数形式（GetPrintCPerLineMethod）
GetIntValue(...):
    return Config.PrintCPerLine               # 直接返回配置值；CanRestructure = true（可常量折叠）
```

## 备注

- zh 文档（Era-Chinese-Documentation）未收录该命令，语义仅以 ecd 为准。
- ecd 文档说默认值为 3，与源码 `Runtime/Config/ConfigData.cs:64` 的默认值 3 一致。
- 该设置实际生效位置：`Runtime/Script/Process.ScriptProc.cs:204/240` 与 `Runtime/Script/Loader/ErbLoader.cs:424` 中，每连续输出 `PrintCPerLine` 个 PRINTC 系指令后自动换行。
- 命令形式与式中函数形式并存，二者读取同一配置项；命令形式只能写变量，式中函数形式用于表达式。
