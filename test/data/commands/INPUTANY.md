# INPUTANY

- **类别**：EE 扩展命令
- **签名**：
  - `INPUTANY`
- **文档来源**：EM+EE 在线文档「INPUTANY」；`EmueraEE_readme.txt`「・INPUTANY」条目；`EmueraEE_changelog.txt` v22「INPUTANY追加」；`ecd/Command.md` 未收录；zh 套件未收录。

## 语义

同时接受整数输入与字符串输入的 INPUT。执行时也接受用鼠标点击画面上 `PRINTBUTTON` 等生成的按钮（含 `[整数]` 形式按钮）作为输入。输入被判定为整数型时代入 `RESULT`，判定为字符串型时代入 `RESULTS`；另一者保持原值。与 INPUT 一样会暂停脚本等待玩家输入，不可缺省默认值。

## 用法

### `INPUTANY`
- 无参数；整数输入 → `RESULT`，字符串输入 → `RESULTS`。
```erb
@SYSTEM_TITLE
PRINTL [0] 输入0
PRINTL [1] 输入1
PRINTBUTTON "[A] 输入A", "A"
PRINTL
INPUTANY
PRINTFORMW 输入结果为\@ RESULTS != "" ? %RESULTS% # {RESULT} \@
;输入 1 时输出「输入结果为1」；点击按钮 A 时输出「输入结果为A」
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:421`（`addFunction(FunctionCode.INPUTANY, new INPUTANY_Instruction())`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:374`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2208`（`INPUTANY_Instruction`，`#region EE_INPUTANY`）；输入分发 `UI/Game/EmueraConsole.cs:1069`（`case InputType.AnyValue`）；`Runtime/InputRequest.cs:10`（`AnyValue = 6`）

```text
class INPUTANY_Instruction : AInstruction
    构造: ArgBuilder = VOID（无参数）; flag = EXTENDED（注释：スキップ不可）
    DoInstruction(exm, func, state):
        req = new InputRequest { InputType = InputType.AnyValue }
        exm.Console.WaitInput(req)          # 挂起等待输入

# 输入到达时的判定（EmueraConsole.ProcessInputString 中）:
case InputType.AnyValue:
    若 long.TryParse(str) 成功:              # 输入可解析为整数
        系统输入则 process.InputSystemInteger(value)
        否则     process.InputInteger(value) # → RESULT
    否则:
        process.InputString(str)             # → RESULTS
# 按钮点击：等待 AnyValue 输入时，整数按钮返回其数值字符串、
# 字符串按钮返回其内容（EmueraConsole.cs:414-417），随后走同一判定
```

## 备注

- 文档与实现一致；注意与 `INPUTMOUSEKEY`（EE 对其另有按钮扩展）及 `BINPUT`（只接受按钮化数值）的区别：INPUTANY 是「数值或字符串都能收」的通用输入。
- 实现中 flag 仅 `EXTENDED`（未带 `IS_PRINT|IS_INPUT`），与普通 INPUT 的标志不同，但对外语义（等待一次输入）一致。
