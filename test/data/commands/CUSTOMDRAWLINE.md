# CUSTOMDRAWLINE

- **类别**：命令
- **签名**：`CUSTOMDRAWLINE <文本>`
- **文档来源**：`ecd/docs/translation/Command.md`「CUSTOMDRAWLINE」小节；`Era-Chinese-Documentation` 未收录本命令

## 语义

用指定的文本反复填充，画满一整行（相当于可自定义分隔线内容的 `DRAWLINE`）。参数是原始文本（不是表达式、也不做 FORM 展开），解析期读入后拼接成占满画面宽度的字符串输出，随后换行。受 `SKIPDISP`（跳过显示）影响：跳过时不输出。文本为空会报错。

## 用法

### `CUSTOMDRAWLINE <文本>`
- 参数：用于填充行的字符串字面量（如 `-{60}` 的内容写法 `----`，直接写即可，不加引号）。
```erb
CUSTOMDRAWLINE -     ; 用 "-" 填满一行，等效 DRAWLINE
CUSTOMDRAWLINE ☆    ; 用 "☆" 填满一行
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:286` → `new CUSTOMDRAWLINE_Instruction()`（flag = METHOD_SAFE | EXTENDED）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:507`（`CUSTOMDRAWLINE_Instruction`）；填充计算在 `UI/Game/EmueraConsole.Print.cs:708`（`printCustomBar`）与 `:726`（`getStBar`）
- 注：索引标注的 `Runtime/Script/Process.ScriptProc.cs:162` 处的 `case FunctionCode.CUSTOMDRAWLINE` 实际被注释掉，运行时走独立指令类。

```text
解析期 CreateArgument(line, exm):
    st = line.PopArgumentPrimitive()        // 取原始文本（非表达式）
    若 st.EOS: 抛出 CodeEE("缺少参数")
    rowStr = st.Substring()
    rowStr = Console.getStBar(rowStr)       // 解析期就展开成占满一行的字符串
    返回 ExpressionArgument(SingleStrTerm(rowStr))，标记 IsConst

运行期 DoInstruction(exm, func, state):
    若 GlobalStatic.Process.SkipPrint: return        // SKIPDISP 时直接跳过
    Console.printCustomBar(func.Argument.ConstStr, true)
    exm.Console.NewLine()

getStBar(barStr):                                    // 填充算法
    builder = barStr
    width = 0
    当 width < Config.DrawableWidth:                 // 逐次整串追加直到超出画面可绘制宽度
        builder.Append(barStr)
        width = 测量(builder, 默认字体)
    当 width > Config.DrawableWidth:                 // 超出后逐字符回删（支持多字符 barStr）
        builder 移除末尾 1 字符
        width = 测量(builder, 默认字体)
    返回 builder

printCustomBar(barStr, isConst):
    若 barStr 为空: 抛出 CodeEE("DRAWLINE字符串为空")
    以常规字体样式 Print(barStr)（isConst=true 时直接打印已展开的串）
```

## 备注

- 参数在**解析期**就由 `getStBar` 展开成定长字符串并标记为常量，运行期只负责打印，因此画面宽度改变（如窗口缩放）后不会重新适配。
- 文档没有写明 `CUSTOMDRAWLINE` 会换行；源码在打印后显式 `NewLine()`，即输出恰好一行。
- 同类的 `DRAWLINEFORM`（支持 FORM 格式）走 `Runtime/Script/Process.ScriptProc.cs:163` 的 switch 分支，运行期才展开；`CUSTOMDRAWLINE` 不支持 FORM。
