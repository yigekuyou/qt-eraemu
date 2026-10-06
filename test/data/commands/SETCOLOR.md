# SETCOLOR

- **类别**：命令
- **签名**：`SETCOLOR <红>, <绿>, <蓝>`
- **签名**：`SETCOLOR <RGB>`
- **文档来源**：`ecd/docs/translation/Command.md`（SETCOLOR / RESETCOLOR 小节）；`Era-Chinese-Documentation/docs/Command.md` 仅在 PRINT 关键字 `D`（「忽略 SetColor 指令」）处侧面提及，无独立小节

## 语义

把文字颜色换为指定的颜色，直至使用 `RESETCOLOR` 指令还原（或再次 `SETCOLOR` 覆盖）。当前文字颜色可以用 `GETCOLOR` 指令获取，默认文字颜色可用 `GETDEFCOLOR` 指令获取。

参数两种写法：分别指定 R、G、B 三个 0～255 的数值，或直接给出 `0xRRGGBB` 形式的单一数值。使用 `GETCOLOR` 获取到的值是 `0xRRGGBB` 单值形式。

三分量写法中任一分量小于 0 或大于 255 时报错；RGB 单值写法不检查范围（拆位后自动截取）。

## 用法

### SETCOLOR <红>, <绿>, <蓝>

- `<红>`、`<绿>`、`<蓝>`：0～255 的数值表达式，任一越界报错。

```erb
SETCOLOR 255, 128, 0
PRINTL 橙色文字
```

### SETCOLOR <RGB>

- `<RGB>`：`0xRRGGBB` 形式的数值表达式，R = 值的高 8 位，G = 中 8 位，B = 低 8 位。

```erb
SETCOLOR 0xFF8000
PRINTL 与 SETCOLOR 255, 128, 0 效果相同
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:271`（`argb[FunctionArgType.SP_COLOR], METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:396`（switch-case `FunctionCode.SETCOLOR`）；最终落点 `UI/Game/EmueraConsole.Print.cs:92`（`EmueraConsole.SetStringStyle(Color)`）

```text
case SETCOLOR:                     // Process.ScriptProc.cs
    colorArg = (SpColorArgument)func.Argument
    if colorArg.RGB != null:       // 单表达式形式 SETCOLOR <RGB>
        colorRGB = colorArg.RGB.GetIntValue(exm)
        R = (colorRGB & 0xFF0000) >> 16
        G = (colorRGB & 0x00FF00) >> 8
        B =  colorRGB & 0x0000FF
    else:                          // 三分量形式 SETCOLOR <R>, <G>, <B>
        R = colorArg.R.GetIntValue(exm)
        G = colorArg.G.GetIntValue(exm)
        B = colorArg.B.GetIntValue(exm)
        if R < 0 or G < 0 or B < 0:       throw CodeEE("SETCOLOR 的参数不能小于 0")
        if R > 255 or G > 255 or B > 255: throw CodeEE("SETCOLOR 的参数不能超过 255")
    c = Color.FromArgb(R, G, B)
    exm.Console.SetStringStyle(c)

EmueraConsole.SetStringStyle(color):   // EmueraConsole.Print.cs
    userStyle.Color = color
    userStyle.ColorChanged = (color != Config.ForeColor)
```

## 备注

- 与 `SETBGCOLOR` 的实现差异：`SETCOLOR` 分支没有 `colorArg.IsConst`（常量）处理路径，`SETBGCOLOR` 有；两者对越界三分量的报错信息相同（都报 SETCOLOR 的文案 `SetcolorArgLessThan0` / `SetcolorArgOver255`，SETBGCOLOR 复用同一错误资源）。
- 之后的 `PRINT` 系指令若带关键字 `D` 会忽略本指令设置的颜色（zh 文档在 PRINT 关键字说明中有提及）。
- `Color.FromArgb` 按字节构造颜色，RGB 单值路径不做 0～255 之外的语义检查，负值或超 `0xFFFFFF` 的值经拆位后可能得到与预期不同的颜色——文档未说明此行为。
