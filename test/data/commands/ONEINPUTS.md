# ONEINPUTS

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令
- **签名**：
  - `ONEINPUTS {<字符串>}`（ecd 文档签名）
  - `ONEINPUTS {<默认字符串>{, <鼠标输入标志>{, <可跳过标志>}}}`（本仓库 EE 扩展后的完整签名，见备注）
- **文档来源**：`ecd/docs/translation/Command.md`「输入」小节（`### ONEINPUTS`）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

仅接受一个字符的自动字符串输入指令，输入一个字符后无需回车即自动进入下一步处理，输入值代入 `RESULTS:0`。其余规格与 `ONEINPUT` 相同：粘贴多位字符时只取第一个字符；可通过参数设置空输入时的默认值，但指定空字符串时参数无效（行为与无参数相同），多位字符串只有第一个字符会成为默认输入值。

`ONEINPUTS` 的情况下，即使保持空字符串按回车，也会被视为输入了空字符串（即把空字符串赋给 `RESULTS` 并继续处理）。与 `INPUTS` 一样可以使用宏表达式（把 `(` `)` 作为字符串使用时需用 `\` 转义）。使用键盘宏的 CONFIG 设置下可能无法正常工作，这是规格。

## 用法

### `ONEINPUTS {<字符串>}`
- `<字符串>`（可省略）：输入空字符串时采用的默认输入值；空字符串参数无效，多位字符串只取第一个字符。输入结果代入 `RESULTS:0`。
```erb
PRINTL 请按任意键输入一个字符
ONEINPUTS
PRINTFORML 你输入了 %RESULTS%
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:204`（`new ONEINPUTS_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:983`（`ONEINPUTS_Instruction`，参数构建器 `SP_INPUTS`（`Runtime/Script/Statements/ArgumentBuilder.cs:1245`），标志 `IS_PRINT | IS_INPUT | EXTENDED`）

```text
case ONEINPUTS:
    arg = (SpInputsArgument)func.Argument        # (Def, Mouse, CanSkip) 三项均可为 null
    req = 新 InputRequest:
        InputType = StrValue
        OneInput = true
    若 arg.Def != null:
        req.HasDefValue = true
        req.DefStrValue = arg.Def 求值           # 旧的多位截断逻辑已注释废弃
    若 arg.Mouse != null:
        req.MouseInput = (arg.Mouse 求值 != 0)
    # EE_INPUT 機能拡張：
    若 arg.CanSkip != null 且 GlobalStatic.Console.MesSkip:
        若 arg.Mouse 求值 == 0:
            RESULTS = arg.Def 求值               # 不等待输入，直接采用默认字符串
        否则:
            RESULTS:1 = arg.Def 求值
    否则:
        exm.Console.WaitInput(req)               # 挂起等待单字符输入
```

参数解析（`SP_INPUTS_ArgumentBuilder`，`Runtime/Script/Statements/ArgumentBuilder.cs:1245`）：第 1 参数为字符串表达式（可为宏表达式 `ToStrFormTerm`）；若还有第 2 参数，必须是整数表达式（鼠标输入标志），不是整数时告警「参数非整数，参数被忽略」且 Mouse/CanSkip 均不生效；参数超过 2 个时告警。

## 备注

- ecd 文档只记载 `{<字符串>}` 一个可选参数；源码实际支持 EE 扩展的最多 3 个参数（默认字符串、鼠标输入标志、可跳过标志）。
- 文档所述「空字符串参数无效、多位截断」的处理逻辑（`Runtime/Script/Statements/Instraction.Child.cs:993-1012`）在当前源码中已被注释掉，默认字符串原样传入输入请求，此点与文档存在差异。
- 「可跳过标志」为本仓库（EE）扩展：消息跳过进行中且设置了该参数时，不等待输入而直接把默认字符串代入 `RESULTS`（或 `RESULTS:1`）。
- zh 套件未收录本命令。
