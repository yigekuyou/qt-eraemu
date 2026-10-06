# ONEINPUT

- **类别**：命令
- **签名**：
  - `ONEINPUT {<数值>}`（ecd 文档签名）
  - `ONEINPUT {<默认值>{, <鼠标输入标志>{, <可跳过标志>}}}`（本仓库 EE 扩展后的完整签名，见备注）
- **文档来源**：`ecd/docs/translation/Command.md`「输入」小节（`### ONEINPUT`）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

仅接受一个字符的自动输入指令，输入一个字符后无需回车即自动进入下一步处理，输入值代入 `RESULT:0`。用粘贴等方式一次贴入多位数字时，只有第一位会被视为输入。

与 `INPUT` 一样可以通过参数设置输入空字符串时的默认输入值。但 `ONEINPUT` 指定负值时参数无效，行为与无参数相同；以多位数字为参数时只有第一位会成为默认输入值。省略参数并输入空字符串时重新要求输入。

使用这些指令时，即使 CONFIG 设置为使用键盘宏也可能无法正常工作，这是规格。单字符输入可以与 `TINPUT` 的性质结合（见 `TONEINPUT`）。

## 用法

### `ONEINPUT {<数值>}`
- `<数值>`（可省略）：输入空字符串时采用的默认输入值；为负值时无效（等同无参数），多位数字只取第一位。输入结果代入 `RESULT:0`。
```erb
PRINTL 请按任意数字键
ONEINPUT
PRINTFORML 你按下了 {RESULT}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:203`（`new ONEINPUT_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:918`（`ONEINPUT_Instruction`，参数构建器 `SP_INPUT`（`Runtime/Script/Statements/ArgumentBuilder.cs:2273`），标志 `IS_PRINT | IS_INPUT | EXTENDED`）

```text
case ONEINPUT:
    arg = (SpInputsArgument)func.Argument        # (Def, Mouse, CanSkip) 三项均可为 null
    req = 新 InputRequest:
        InputType = IntValue
        OneInput = true
    若 arg.Def != null:
        req.HasDefValue = true
        req.DefIntValue = arg.Def 求值           # 注意：负值/多位截断的旧逻辑已注释废弃
    若 arg.Mouse != null:
        req.MouseInput = (arg.Mouse 求值 != 0)
    # EE_INPUT 機能拡張：
    若 arg.CanSkip != null 且 GlobalStatic.Console.MesSkip（消息跳过进行中）:
        若 arg.Mouse 求值 == 0:
            RESULT = arg.Def 求值                # 不等待输入，直接采用默认值
        否则:
            RESULT:1 = arg.Def 求值
    否则:
        exm.Console.WaitInput(req)               # 挂起等待单字符输入
```

参数解析（`SP_INPUT_ArgumentBuilder`，`Runtime/Script/Statements/ArgumentBuilder.cs:2273`）：所有参数必须是整数表达式，否则告警并中止解析；0~3 个参数分别填入 `SpInputsArgument(Def, Mouse, CanSkip)`。

## 备注

- ecd 文档只记载 `{<数值>}` 一个可选参数；源码实际支持 EE 扩展的最多 3 个参数（默认值、鼠标输入标志、可跳过标志）。文档中「负值参数无效、多位数字只取第一位」的截断处理逻辑在当前源码中已被注释掉（`Runtime/Script/Statements/Instraction.Child.cs:928-949`），即现在的默认值原样传给输入请求，此点与文档存在差异。
- 「可跳过标志」是本仓库（EE）扩展：消息跳过（MesSkip）进行中且设置了该参数时，不等待输入而直接把默认值代入 `RESULT`（或 `RESULT:1`）。
- 单字符输入的最终解释（按键过滤、粘贴截取等）在控制台的输入处理（`EmueraConsole.doInputToEmueraProgram`，`InputRequest.OneInput = true`）中完成。
- zh 套件未收录本命令（仅 `Config_File.md` 提及 ONEINPUT 系命令的鼠标多字符输入配置项）。
