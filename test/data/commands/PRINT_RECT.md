# PRINT_RECT

- **类别**：命令（EM_私家版_HTMLパラメータ拡張）
- **签名**：`PRINT_RECT <数值表达式>`
- **签名**：`PRINT_RECT <数值表达式>, <数值表达式>, <数值表达式>, <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「PRINT_RECT `<数值表达式>`」与「PRINT_RECT `<数值表达式>`, `<数值表达式>`, `<数值表达式>`, `<数值表达式>`」两节；`zh/Command.md` 未收录。

## 语义

在当前行内绘制一个长方形（相当于 `HTML_PRINT` 指令的 `<shape type='rect'>` 标签）。颜色可用 `SETCOLOR` 像改变字体颜色一样改变。

- 1 参数形式：只给宽度，参数为相对字号的百分比。`PRINT_RECT 400` 画一个宽度为字号 400% 的长方形，等价于 `PRINT_RECT 0, 0, 400, 100`。
- 4 参数形式：依次为 x、y、宽度、高度，均为相对字号的百分比（x、y 是绘制位置偏移，y 相对字号而非行高，见 HTML_PRINT 的 shape 说明）。

EM 扩展下每个参数还可带 `px` 后缀（如 `100px`），此时按像素而非字号百分比解释。

本命令属于 EM_私家版_HTMLパラメータ拡張，`PRINT_RECT` 是 EXTENDED 标记的扩展命令；参数个数为 1 或 4 之外的个数会在解析期报错。

## 用法

### `PRINT_RECT <宽度%>`
- `<宽度%>`：数值表达式，长方形宽度为字号的百分之多少。

```erb
SETCOLOR 255,000,000
PRINT_RECT 400        ; 画一个宽为字号 400% 的红色长方形（等价于 0,0,400,100）
```

### `PRINT_RECT <x%>, <y%>, <宽度%>, <高度%>`
- `<x%>`、`<y%>`：绘制位置的偏移，为字号的百分之多少（y 基于字号而非行高）。
- `<宽度%>`、`<高度%>`：长方形的宽、高，为字号的百分之多少。

```erb
SETCOLOR 255,000,000
PRINT_RECT 0, 25, 400, 50    ; 高度为字号 50%、在行的上下居中的长方形
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:389`（`new PRINT_RECT_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:441-470`（类 `PRINT_RECT_Instruction`，flag = `EXTENDED | METHOD_SAFE`）；参数解析 `Runtime/Script/Statements/ArgumentBuilder.cs:238`（`SP_PRINT_SHAPE_ArgumentBuilder(4)`）与 `:312-352`；最终绘制 `UI/Game/EmueraConsole.Print.cs:490-495`（`PrintShape`）

```text
解析期（SP_PRINT_SHAPE_ArgumentBuilder，maxArg=4）：
    逐个读参数，支持以 px 结尾的“关键字 px”写法（isPx = 该词非空且非逗号）。
    若参数个数既不是 1 也不是 4：警告参数个数不符，参数为 null。
    超过 4 个参数：警告参数过多。

执行期（PRINT_RECT_Instruction.DoInstruction）：
    若 Process.SkipPrint：直接返回。
    arg = (SpPrintShapeArgument)func.Argument
    若 arg == null：抛出 CodeEE（参数无效）。
    param = 与 arg.Param 等长的 MixedNum 数组
    对每个 i：param[i] = { num = (int)arg.Param[i].num.GetIntValue(exm), isPx = arg.Param[i].isPx }
    控制台.PrintShape("rect", param):
        part = ConsoleShapePart.CreateShape("rect", param, 当前文字色, 按钮色, false)
        追加到 printBuffer（与后续 PRINT 内容同行显示）
```

## 备注

- `ecd/Command.md` 给出的两节文档没有提到 EM 扩展的 `px` 后缀；源码（MixedNum.isPx 与 SP_PRINT_SHAPE_ArgumentBuilder）显示每个参数都可以 `100px` 形式按像素指定，这是 EM_私家版_HTMLパラメータ拡張 的一部分，文档（按原版 Emuera 编写）与源码在此存在差异。
- `zh/Command.md` 未收录。
- 与文档“宽度为字号参数百分之多少”的说法对应，1 参数形式等价于 4 参数形式的 `0, 0, 宽度, 100`（HTML_PRINT.md shape 节）。
