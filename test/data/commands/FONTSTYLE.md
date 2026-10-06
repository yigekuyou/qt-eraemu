# FONTSTYLE

- **类别**：命令
- **签名**：`FONTSTYLE <数值表达式>`（参数可省略，省略时按 0 处理）
- **文档来源**：`ecd/docs/translation/Command.md`「文字样式相关」`### FONTSTYLE` 小节；`Era-Chinese-Documentation`（zh 套件）未收录该命令

## 语义

用一个数值整体设定当前文字样式。各二进制位含义：0 = 默认（普通）、1 = 加粗、2 = 倾斜、4 = 删除线、8 = 下划线；可用位或组合，如 `FONTSTYLE 3` 为加粗＋倾斜（等同同时 `FONTBOLD` + `FONTITALIC`），`FONTSTYLE 0` 等同 `FONTREGULAR`。注意本命令是"整体替换"而不是在现有样式上叠加。无返回值，只影响之后的 PRINT 系输出。

## 用法

### FONTSTYLE <数值表达式>
- `<数值表达式>`：样式位的组合值（0~15）。可省略，省略时视为 0（即恢复普通样式）。

```erb
FONTSTYLE 1 + 2
PRINTL 加粗＋倾斜
FONTSTYLE 5
PRINTL 加粗＋删除线
FONTSTYLE 0
PRINTL 普通
FONTSTYLE
PRINTL 省略参数同样恢复普通
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:284`（`argb[FunctionArgType.INT_EXPRESSION_NULLABLE]`，flag = METHOD_SAFE | EXTENDED；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:184`）
- 参数构建：`Runtime/Script/Statements/ArgumentBuilder.cs:183` 映射到 `INT_EXPRESSION_ArgumentBuilder(true)`（`Runtime/Script/Statements/ArgumentBuilder.cs:1328`）——参数个数为 0 时用 `SingleLongTerm(0)` 填充且不告警
- 实现：`Runtime/Script/Process.ScriptProc.cs:483`（switch-case，无独立类）

```text
case FONTSTYLE:
    iValue ← 参数为常量 ? 常量值 : 求值表达式（省略时为 0）
    fs ← FontStyle.Regular            # 从"空白"样式出发，整体替换
    若 iValue & 1 ≠ 0: fs |= Bold
    若 iValue & 2 ≠ 0: fs |= Italic
    若 iValue & 4 ≠ 0: fs |= Strikeout
    若 iValue & 8 ≠ 0: fs |= Underline
    exm.Console.SetStringStyle(fs)
```

`SetStringStyle` 位于 `UI/Game/EmueraConsole.Print.cs:91`。与 `FONTBOLD`/`FONTITALIC` 不同，此路径没有非 Windows 平台短路判断。

## 备注

- ecd 文档只列出 0/1/2/4/8 五种位；源码同样是按这四个位掩码处理，任何更高位（16 以上）会被忽略（不报错）。
- 省略参数的行为（按 0 处理）来自源码 `INT_EXPRESSION_NULLABLE` 参数构建，ecd 文档未明说。
- 其余无文档与源码冲突。
