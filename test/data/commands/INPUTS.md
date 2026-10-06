# INPUTS

- **类别**：命令
- **签名**：
  - `INPUTS`
  - `INPUTS <默认字符串>`
  - `INPUTS <默认字符串>, <是否允许鼠标输入>`
  - `INPUTS <默认字符串>, <是否允许鼠标输入>, <消息快进时是否使用默认值>`（EM/EE 扩展参数）
- **文档来源**：`ecd/docs/translation/Command.md`「输入·等待」INPUTS 小节；zh 套件 `Command.md` 未收录（`zh/ERB_File_Format.md`、`zh/General.md` 等处有零散提及）。

## 语义

等待用户输入字符串，结果赋给 `RESULTS`。

与 Eramaker 基本相同，但可以传参数设置输入空字符串时的默认输入值：省略参数而用户输入空字符串时，与旧版一样把空字符串赋给 `RESULTS` 并继续处理；给定默认字符串时，空输入即采用默认字符串。

第 2 参数非 0 时允许鼠标输入；第 3 参数用于消息快进（MesSkip）状态（EE 扩展，见备注）。支持宏表达式（与 `TINPUTS` 相同，要把 `(` `)` 当普通字符需用 `\` 转义——此点文档记在 `TINPUTS` 下，`INPUTS` 同族）。

## 用法

### `INPUTS` / `INPUTS <默认字符串>` / `INPUTS <默认字符串>, <鼠标>` / `INPUTS <默认字符串>, <鼠标>, <跳过时取默认值>`
- `<默认字符串>`：字符串表达式（支持 FORM 格式），空输入时作为结果；可省略。
- `<鼠标>`：整数表达式，非 0 时允许鼠标输入；可省略。
- `<跳过时取默认值>`：整数表达式；消息快进状态下配合前两个参数使用（EE 扩展）；可省略。
```erb
PRINTL 请输入名字（直接回车则为「无名」）
INPUTS 无名
PRINTFORML 名字是 %RESULTS%
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:195`（`new INPUTS_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:862`（`INPUTS_Instruction`，flag = `IS_PRINT | IS_INPUT`）；参数解析在 `Runtime/Script/Statements/ArgumentBuilder.cs:1245`（`SP_INPUTS_ArgumentBuilder`：第 1 参数按 FORM 字符串解析，其余最多 1 个整数参数（鼠标）、再最多 1 个（跳过标志））

```text
参数解析（SP_INPUTS_ArgumentBuilder）:
    无参数 → SpInputsArgument(null, null, null)
    第 1 参数：词法分析为 FORM 字符串项 → Def（字符串表达式）
    第 2 参数：整数表达式 → Mouse；不是整数 → 警告“因不是整数而忽略参数”
    第 3 参数：整数表达式 → CanSkip；多余参数 → 警告“参数过多”

DoInstruction(exm, func, state):
    arg = (SpInputsArgument)func.Argument
    req = new InputRequest { InputType = StrValue }
    若 arg.Def != null:
        req.HasDefValue = true
        req.DefStrValue = arg.Def.GetStrValue(exm)
    若 arg.Mouse != null:
        req.MouseInput = (arg.Mouse.GetIntValue(exm) != 0)
    exm.Console.Window.ApplyTextBoxChanges()
    若 arg.CanSkip != null 且 GlobalStatic.Console.MesSkip:
        # 不等待输入，直接采用默认字符串
        若 arg.Mouse.GetIntValue(exm) == 0:
            GlobalStatic.VEvaluator.RESULTS = arg.Def.GetStrValue(exm)
        否则:
            GlobalStatic.VEvaluator.RESULTS_ARRAY[1] = arg.Def.GetStrValue(exm)
    否则:
        exm.Console.WaitInput(req)    # 阻塞等待输入，结果由控制层写入 RESULTS
```

## 备注

- ecd 文档只描述「默认字符串」参数；鼠标输入、快进取默认值为 EM/EE 扩展，文档未收录。
- 源码疑点（与 `INPUT` 相同）：快进分支直接解引用 `arg.Mouse`，若只给了默认值与 CanSkip 而未给鼠标参数，理论上会空引用异常。
- 第 2/3 参数的解析方式与 `INPUT`（全部按整数表达式 popTerms）不同：`INPUTS` 的第 1 参数经 `AnalyseFormattedString`（支持 FORM 语法），第 2 个之后才按整数表达式解析。
