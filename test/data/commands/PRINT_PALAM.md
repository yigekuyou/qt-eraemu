# PRINT_PALAM

- **类别**：命令
- **签名**：`PRINT_PALAM <数值表达式（已登录角色编号）>`
- **文档来源**：`ecd/docs/translation/Command.md` 未收录独立小节；语义见 `Era-Chinese-Documentation/docs/ERB_File_Format.md`（「显示训练专用的数据」小节）与 `ecd/EraBasic_Structure.md:181`（「使用 `PRINT_PALAM` 命令来显示训练中的参数」）；实现细节来自源码。

## 语义

显示指定角色训练中的参数（`PALAM` 数组）。对槽位 0～99 逐个调用内部取串函数：若该槽位的 `PALAM` 值为 0 且 `PALAMNAME` 为空则跳过；否则按 `PALAMLV` 数组的档位选择条形符号，输出形如 `参数名[####......]   500` 的字符串（名字 + 10 格条形图 + 右对齐 6 位数值），并以 `PRINTC` 的右对齐格式打印。

每打印 `PRINTC 并列数`（设置项，Eramaker 原作为 3，见 `PRINTCPERLINE`）个就换一次行。全部打印完后刷新显示。

角色编号越界时抛出运行时错误。100 及以后的槽位（否定的珠等）不显示。

## 用法

### `PRINT_PALAM <角色编号>`
- `<角色编号>`：数值表达式，已登录角色的编号（0 起）。

```erb
PRINT_PALAM 0        ; 显示主角（0 号角色）的参数列表
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:184`（`argb[FunctionArgType.INT_EXPRESSION], METHOD_SAFE`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:189-210`（switch-case `FunctionCode.PRINT_PALAM`）；取串函数在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:991-1052`（`GetCharacterParamString`）

```text
若处于 skipPrint 状态：什么都不做。

target = 参数求值（整数）。
count = 0
对 i 从 0 到 99：
    s = GetCharacterParamString(target, i):
        若 target 越界：抛出 CodeEE（角色编号越界）。
        param = 角色的 PALAM[i]；paramlv = 主变量区的 PALAMLV；name = CSV 的 PALAMNAME[i]
        若 param == 0 且 name 为空：返回 null（跳过）
        若 name 为 null：name = ""
        c = '-'；border = paramlv[1]
        若 param >= border：c = '='；border = paramlv[2]
        若 param >= border：c = '>'; border = paramlv[3]
        若 param >= border：c = '*'
        bar = "["
        若 border <= 0 或 param >= border：bar += c 重复 10 次
        否则若 param <= 0：bar += '.' 重复 10 次
        否则：n = param * 10 / border；bar += c 重复 n 次 + '.' 重复 (10-n) 次
        bar += "]"
        返回 $"{name}{bar}{param,6}"（数值右对齐 6 位）
    若 s != null：
        控制台.PrintC(s, 右对齐=true)；count++
        若 Config.PrintCPerLine > 0 且 count % PrintCPerLine == 0：PrintFlush（换行）
控制台.PrintFlush(false)；控制台.RefreshStrings(false)
```

## 备注

- `ecd/Command.md` 没有本命令的独立小节；条形图档位（`-`/`=`/`>`/`*`，由 `PALAMLV:1..4` 决定边界）以及「只显示槽位 0～99」的限制仅见于源码。
- `zh/Command.md` 未收录；`zh/ERB_File_Format.md:1191` 仅一句「`PRINT_PALAM`：显示角色训练中的参数」。
- 源码注释明确说明「100 以降は否定の珠とかなので表示しない」（100 以后是否定之珠之类，故不显示）。
