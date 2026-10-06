# PRINT_SPACE

- **类别**：命令（EM_私家版_HTMLパラメータ拡張）
- **签名**：`PRINT_SPACE <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「PRINT_SPACE `<数值表达式>`」节；`zh/Command.md` 未收录。

## 语义

在当前行中创建一个大小为「字号 × 参数%」的空白（相当于 `HTML_PRINT` 指令的 `<shape type='space'>` 标签）。即参数为 400 时，空白宽度约为字号的 400%，大致相当于 4 个全角空格（见 HTML_PRINT.md shape 节）。

EM 扩展下参数还可带 `px` 后缀（如 `100px`），此时按像素而非字号百分比解释。

## 用法

### `PRINT_SPACE <宽度%>`
- `<宽度%>`：数值表达式，空白宽度为字号的百分之多少。

```erb
PRINT ほげ
PRINT_SPACE 200       ; 插入字号的 200% 宽的空白
PRINT ぴよ
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:390`（`new PRINT_SPACE_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:474-505`（类 `PRINT_SPACE_Instruction`，flag = `EXTENDED | METHOD_SAFE`）；参数解析 `Runtime/Script/Statements/ArgumentBuilder.cs:239`（`SP_PRINT_SHAPE_ArgumentBuilder(1)`）与 `:312-352`；最终绘制 `UI/Game/EmueraConsole.Print.cs:490-495`（`PrintShape`）

```text
解析期（SP_PRINT_SHAPE_ArgumentBuilder，maxArg=1）：
    逐个读参数，支持以 px 结尾的写法（isPx = 该词非空且非逗号）。
    参数个数不是 1 也不是 4：警告参数个数不符（对 PRINT_SPACE 实际只应为 1），
      参数为 null。超过 1 个：警告参数过多。

执行期（PRINT_SPACE_Instruction.DoInstruction）：
    若 Process.SkipPrint：直接返回。
    arg = (SpPrintShapeArgument)func.Argument
    若 arg == null：抛出 CodeEE（参数无效）。
    param = 与 arg.Param 等长的 MixedNum 数组
    对每个 i：param[i] = { num = (int)arg.Param[i].num.GetIntValue(exm), isPx = arg.Param[i].isPx }
    控制台.PrintShape("space", param):
        part = ConsoleShapePart.CreateShape("space", param, 当前文字色, 按钮色, false)
        追加到 printBuffer（与后续 PRINT 内容同行显示）
```

## 备注

- 源码中被注释掉的旧实现只接受单个整数参数（`INT_EXPRESSION`）；现行 EM 扩展实现使用与 PRINT_RECT 相同的 `SP_PRINT_SHAPE_ArgumentBuilder`，理论上 1 或 4 个参数都能通过解析（个数校验允许 1 或 4），但语义上只有第 1 个（宽度）有效。
- 文档未提及 `px` 后缀；源码支持按像素指定，为 EM 私家版扩展，与文档存在差异。
- `zh/Command.md` 未收录。
