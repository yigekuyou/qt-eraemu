# PRINT_IMG

- **类别**：命令
- **签名**：PRINT_IMG <图片文件名>
- **文档来源**：`ecd/docs/translation/Command.md`「### PRINT_IMG `<字符串表达式>`」；Era-Chinese-Documentation 未收录本命令

## 语义

在当前输出行中显示指定图像，相当于 `HTML_PRINT` 指令中 `<img>` 标签的行内版本。最基本的用法是只给图片文件名（字符串表达式）。本仓库实现为 EM 私家版扩展版本：参数在文件名之外还允许最多 5 个附加参数——两个可选字符串参数（备用图片名 b/m，对应 `<img>` 的 b/m 属性）和三个可选的数值参数（高度、宽度、y 偏移），数值后可跟 `px` 关键字表示像素，否则按字号百分比解释。受 `SKIPDISP` 影响。

## 用法

### PRINT_IMG <图片文件名>

- `<图片文件名>`：字符串表达式（必填），为图像资源文件名。
- 输出后不换行，图像作为行内元素追加到输出缓冲。

```erb
PRINT_IMG "title.png"
```

### PRINT_IMG <图片文件名>, <b图片名>?, <m图片名>?, <高度>?{,px}, <宽度>?{,px}, <y偏移>?{,px}

- `<b图片名>`、`<m图片名>`：可选字符串参数（EM 私家版 `<img>` 标签的 b/m 扩展属性）。
- 三个数值参数依出现顺序对应：高度、宽度、y 偏移；每个数值后可紧跟 `px` 关键字表示以像素为单位（如 `40px`），省略时按字号的百分比。
- 多余参数（数值超过 3 个）在解析期报「参数过多」警告。

```erb
PRINT_IMG "chara.png", , , 100, 100
; 以字号百分比指定 100x100 显示

PRINT_IMG "chara.png", "b.png", "m.png", 64px, 64px, 0px
; 指定备用图与像素尺寸
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:388`（`addFunction(FunctionCode.PRINT_IMG, new PRINT_IMG_Instruction())`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:354`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:402`（`PRINT_IMG_Instruction`）；参数解析 `Runtime/Script/Statements/ArgumentBuilder.cs:251`（`SP_PRINT_IMG_ArgumentBuilder`）；最终绘制 `UI/Game/EmueraConsole.Print.cs:481`（`PrintImg`）

```text
构造期（PRINT_IMG_Instruction 构造函数）：
    flag = EXTENDED | METHOD_SAFE
    参数构造器 = SP_PRINT_IMG（EM 私家版 HTML 参数扩展，替换原 STR_EXPRESSION）

执行期 DoInstruction(exm, func, state)：
    若 Process.SkipPrint → 直接返回
    arg = (SpPrintImgArgument)func.Argument
    若 arg == null → 抛 CodeEE（参数非法）
    strb = arg.Nameb?.GetStrValue(exm)（可空）；空串按 null 处理
    strm = arg.Namem?.GetStrValue(exm)（可空）
    height = 若 Param[1] 存在 → { num = Param[1].GetIntValue(exm), isPx = Param[1].isPx }
    width  = 若 Param[0] 存在 → { num = Param[0].GetIntValue(exm), isPx = Param[0].isPx }
    ypos   = 若 Param[2] 存在 → { num = Param[2].GetIntValue(exm), isPx = Param[2].isPx }
    Console.PrintImg(arg.Name.GetStrValue(exm), strb, strm, height, width, ypos)：
        printBuffer.Append(new ConsoleImagePart(name, nameb, namem, height, width, ypos))

参数解析（SP_PRINT_IMG_ArgumentBuilder.CreateArgument）：
    第 1 参：表达式（图片名），缺失 → 警告「不能省略参数」并返回 null
    之后循环读参数（最多再收 3 个数值进 param）：
        字符串类型参数：依出现位置填 nameb（第 2 参）→ namem（第 3 参）；
            若已开始收数值参数或位置越界 → 警告「参数不正确」
        数值类型参数：追加进 param；
            isPx = 当前词后是否紧跟 "px" 关键字
    返回 SpPrintImgArgument(name, nameb, namem, param)
```

## 备注

- ecd 文档只记载了单参数用法（「相当于 HTML_PRINT 的 img 标签」）；多参数（b/m 图片名、高度/宽度/y 偏移、px 单位）是本仓库 EM 私家版扩展，见源码内 `EM_私家版_HTMLパラメータ拡張` 注释区，文档未记载。
- 与 ecd 文档签名「字符串表达式」一致：第 1 参数支持表达式（如变量拼接的文件名），而非仅字面量。
- 数值参数顺序注意：`Param` 数组按出现顺序为 [高度, 宽度, y 偏移]，与 `PrintImg(name, nameb, namem, height, width, ypos)` 的形参顺序对应。
