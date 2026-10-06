# SETFONT

- **类别**：命令
- **签名**：`SETFONT <字符串表达式>`（参数可省略）
- **文档来源**：`ecd/docs/translation/Command.md`（SETFONT / GETFONT 小节）；`Era-Chinese-Documentation/docs/` 未收录该命令小节

## 语义

将当前使用的字体设为指定字体名的字体。

- 省略参数或以空字符串为参数时，恢复为设置中指定的默认字体。
- 指定字体不存在时，将以字体 `Microsoft Sans Serif` 替代显示。
- 由于指定的字体有可能未被安装，建议先使用 `CHKFONT` 检查字体是否已安装。
- 当前字体名可用 `GETFONT` 获取。

## 用法

### SETFONT <字符串表达式>

- `<字符串表达式>`：字体名称；可省略（省略或空串 = 默认字体）。

```erb
PRINTL abc123啊哦呃(默认字体)
CHKFONT "ＭＳ Ｐゴシック"
IF RESULT
	SETFONT "ＭＳ Ｐゴシック"
	PRINTL abc123啊哦呃(ＭＳ Ｐゴシック)
ENDIF
STR:0 = ＭＳ Ｐ明朝
CHKFONT STR:0
IF RESULT
	SETFONT STR:0
	PRINTL abc123啊哦呃(ＭＳ Ｐ明朝)
ENDIF
SETFONT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:290`（`argb[FunctionArgType.STR_EXPRESSION_NULLABLE], METHOD_SAFE | EXTENDED`，参数类型为「可空字符串表达式」）
- 实现：`Runtime/Script/Process.ScriptProc.cs:501`（switch-case `FunctionCode.SETFONT`）；最终落点 `UI/Game/EmueraConsole.Print.cs:93`（`EmueraConsole.SetFont`）

```text
case SETFONT:                        // Process.ScriptProc.cs
    if func.Argument.IsConst:
        str = func.Argument.ConstStr     // 常量字符串（含空串）
    else:
        str = func.Argument.Term.GetStrValue(exm)   // 求值字符串表达式
    exm.Console.SetFont(str)

EmueraConsole.SetFont(fontname):     // EmueraConsole.Print.cs
    if not string.IsNullOrEmpty(fontname):
        userStyle.Fontname = fontname   // 设置指定字体
    else:
        userStyle.Fontname = Config.FontName   // 空/未指定 → 配置默认字体
```

## 备注

- 文档称「指定字体不存在时，将替代为字体 `Microsoft Sans Serif`」；这一替换发生在之后的绘制层（WinForms 的 Font 回退机制），`SetFont` 本身只存字符串、不做存在性检查——因此文档行为成立，但源码中无显式替换代码。
- 省略参数与传空串在实现上是同一条路径（`IsNullOrEmpty`），与文档「省略参数时或以空字符串为参数时」的表述一致。
- 参数类型 `STR_EXPRESSION_NULLABLE` 允许省略；本指令与许多 STR 类指令（如 SETCOLORBYNAME 要求常量）不同，接受任意字符串表达式。
- `zh/` 文档套件未收录本命令，无从交叉核对。
