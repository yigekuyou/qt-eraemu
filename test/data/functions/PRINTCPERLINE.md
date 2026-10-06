# PRINTCPERLINE

- **类别**：式中函数
- **签名**：int PRINTCPERLINE()
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；`ecd/Command.md`「### PRINTCPERLINE」

## 语义

无参数式中函数，返回当前配置中「PRINTC 并列数量」设置（即一行最多能并排显示多少个 `PRINTC` 项），存入 `RESULT:0`（以函数形态直接作为表达式值返回）。该设置默认值为 3。

同名命令形态：`PRINTCPERLINE` 既是式中函数也在 ecd/Command.md 中按指令小节记载，两者语义相同（读取同一配置值）。本仓库中它同时通过 `Runtime/Script/Statements/FunctionIdentifier.cs` 的 SP_GETINT 通道注册为命令形态（Process.ScriptProc.cs:554），本文档以式中函数形态为主。

## 用法

### int PRINTCPERLINE()
- 无参数（`()` 必须写，以区分变量）。
- 返回值：配置项「PRINTC 并列数量」（PRINTCを並べる数，默认 3）。
```erb
IF PRINTCPERLINE() <= 2
	PRINTL 一行放不下几个按钮。
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:65`（`["PRINTCPERLINE"] = new GetPrintCPerLineMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2841`（`GetPrintCPerLineMethod`）

```text
构造：返回类型 = long；参数列表 = []；CanRestructure = true（允许常量折叠）。

GetIntValue(exm, args):
    返回 Config.PrintCPerLine
```

`Config.PrintCPerLine` 来自配置项 `PrintCPerLine`（`Runtime/Config/ConfigData.cs:64`，名称「PRINTCを並べる数」，默认 3）。

## 备注

- ecd/Command.md 小节描述为「返回到 RESULT:0 中」，这是命令形态的叙述；式中函数形态直接返回该值，语义一致。
- ecd 文档说默认值为 3，源码 `Runtime/Config/ConfigData.cs` 中默认值同为 3，一致。
- zh 套件未收录本函数。
