# INPUT

- **类别**：命令
- **签名**：
  - `INPUT`
  - `INPUT <默认值>`
  - `INPUT <默认值>, <是否允许鼠标输入>`
  - `INPUT <默认值>, <是否允许鼠标输入>, <消息快进时是否使用默认值>`（EM/EE 扩展参数）
- **文档来源**：`ecd/docs/translation/Command.md`「输入·等待」INPUT 小节；zh 套件 `Command.md` 未收录（仅 `ERB_File_Format.md`、`Resource.md` 等处顺带提及）。

## 语义

等待用户输入一个数值，输入结果赋给 `RESULT`（通过 `RESULT` 传回脚本）。

与 Eramaker 基本相同，但可以传参数设置输入空字符串时采用的默认输入值：省略参数而用户输入空字符串时，与旧版一样要求重新输入；给定默认值时，空输入即采用默认值。

第 2 参数非 0 时允许用鼠标（点击按钮等）代替键盘输入；第 3 参数与消息快进（MesSkip）状态相关（EE 扩展，见备注）。

## 用法

### `INPUT` / `INPUT <默认值>` / `INPUT <默认值>, <鼠标>` / `INPUT <默认值>, <鼠标>, <跳过时取默认值>`
- `<默认值>`：整数表达式，用户直接回车（空输入）时作为输入结果；可省略（省略时空输入会重新要求输入）。
- `<鼠标>`：整数表达式，非 0 时允许鼠标输入；可省略。
- `<跳过时取默认值>`：整数表达式；在消息快进状态下配合前两个参数使用（EE 扩展）；可省略。
```erb
PRINTL 请输入数值（直接回车取 0）
INPUT 0
PRINTFORML 输入结果 = {RESULT}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:194`（`new INPUT_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:806`（`INPUT_Instruction`，flag = `IS_PRINT | IS_INPUT`）；参数解析在 `Runtime/Script/Statements/ArgumentBuilder.cs:2273`（`SP_INPUT_ArgumentBuilder`，最多 3 个整数参数，全部可省）

```text
参数解析（SP_INPUT_ArgumentBuilder）:
    解析出最多 3 个整数表达式 → SpInputsArgument(Def, Mouse, CanSkip)
    某参数类型不是整数 → 警告并整体解析失败
    全部可省略（minArg = 0），省略的成员为 null

DoInstruction(exm, func, state):
    arg = (SpInputsArgument)func.Argument
    req = new InputRequest { InputType = IntValue }
    若 arg.Def != null:
        req.HasDefValue = true
        req.DefIntValue = arg.Def.GetIntValue(exm)
    若 arg.Mouse != null:
        req.MouseInput = (arg.Mouse.GetIntValue(exm) != 0)
    exm.Console.Window.ApplyTextBoxChanges()   # 应用输入框状态变化
    若 arg.CanSkip != null 且 GlobalStatic.Console.MesSkip（正在消息快进）:
        # 不等待输入，直接采用默认值
        若 arg.Mouse.GetIntValue(exm) == 0:
            GlobalStatic.VEvaluator.RESULT = arg.Def.GetIntValue(exm)
        否则:
            GlobalStatic.VEvaluator.RESULT_ARRAY[1] = arg.Def.GetIntValue(exm)
    否则:
        exm.Console.WaitInput(req)    # 阻塞等待用户输入，结果由控制层写入 RESULT
```

## 备注

- ecd 文档只描述到「第 1 参数默认值」；「鼠标输入」「快进时采用默认值」是 EM 私家版/EE 扩展参数，文档未收录，仅见于源码。
- 源码疑点：在 `CanSkip != null && MesSkip` 的快进分支里，代码直接使用 `arg.Mouse`，但该分支并未保证 `Mouse != null`（`INPUT 0,,1` 这类写法理论上会触发空引用异常）；正常写法（同时给默认值与鼠标参数）不受影响。文档自然也未描述此边界。
- 快进分支把结果写入 `RESULT` 或 `RESULT_ARRAY[1]` 的二选一逻辑与正常路径（控制层写入）的保存位置需保持一致，这是 EE 扩展行为。
- `INPUT` 的输入结果经控制层写入 `RESULT`，`DoInstruction` 本身不回写 `RESULT`（快进分支除外）。
