# SETBGCOLOR

- **类别**：命令
- **签名**：`SETBGCOLOR <红>, <绿>, <蓝>`
- **签名**：`SETBGCOLOR <RGB>`
- **文档来源**：`ecd/docs/translation/Command.md`（SETBGCOLOR 小节）；`Era-Chinese-Documentation/docs/` 未收录该命令小节

## 语义

把文字显示的背景色改为指定颜色，直到被下一次 `SETBGCOLOR` 或 `RESETBGCOLOR` 改变。当前背景色可用 `GETBGCOLOR` 获取，默认背景色可用 `GETDEFBGCOLOR` 获取。

参数两种写法：分别指定 R、G、B 三个 0～255 的数值，或直接给出 `0xRRGGBB` 形式的单一数值。RGB 单值写法不检查范围（拆位后自动截取），三分量写法中任一分量小于 0 或大于 255 时报错。

出于防止屏幕闪烁的安全考虑，背景色变更后有最短间隔限制：间隔过短的连续变更会被强制等待（文档表述为「0.2 秒内再次变更会被强制等待 0.2 秒」）。

## 用法

### SETBGCOLOR <红>, <绿>, <蓝>

- `<红>`、`<绿>`、`<蓝>`：0～255 的数值表达式，任一越界报错。

```erb
SETBGCOLOR 0, 0, 128
PRINTL 蓝底文字
```

### SETBGCOLOR <RGB>

- `<RGB>`：`0xRRGGBB` 形式的数值表达式，R = 值的高 8 位，G = 中 8 位，B = 低 8 位。

```erb
SETBGCOLOR 0x000080
PRINTL 与 SETBGCOLOR 0, 0, 128 相同
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:274`（`argb[FunctionArgType.SP_COLOR], METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:436`（switch-case `FunctionCode.SETBGCOLOR`）；最终落点 `UI/Game/EmueraConsole.Print.cs:111`（`EmueraConsole.SetBgColor`）

```text
case SETBGCOLOR:                     // Process.ScriptProc.cs
    colorArg = (SpColorArgument)func.Argument
    if colorArg.IsConst:             // 参数是常量数值（SP_COLOR 特化：常量按 RGB 单值处理）
        colorRGB = colorArg.ConstInt
        R = (colorRGB & 0xFF0000) >> 16
        G = (colorRGB & 0x00FF00) >> 8
        B =  colorRGB & 0x0000FF
    else if colorArg.RGB != null:    // 单表达式形式 SETBGCOLOR <RGB>
        colorRGB = colorArg.RGB.GetIntValue(exm)
        R = (colorRGB >> 16) & 0xFF; G = (colorRGB >> 8) & 0xFF; B = colorRGB & 0xFF
    else:                            // 三分量形式 SETBGCOLOR <R>, <G>, <B>
        R = colorArg.R.GetIntValue(exm)
        G = colorArg.G.GetIntValue(exm)
        B = colorArg.B.GetIntValue(exm)
        if R < 0 or G < 0 or B < 0:  throw CodeEE("SETBGCOLOR 的参数不能小于 0")
        if R > 255 or G > 255 or B > 255: throw CodeEE("SETBGCOLOR 的参数不能超过 255")
    c = Color.FromArgb(R, G, B)
    exm.Console.SetBgColor(c)

EmueraConsole.SetBgColor(color):     // EmueraConsole.Print.cs
    bgColor = color
    forceTextBoxColor = true
    if 未重绘 且 滚动条不在最底端:
        return                        // 只记旗标，等下次重绘时再上色
    // 防闪烁节流：
    if _drawStopwatch == null: 启动计时器
    else: while 计时 < 1000/FPS 毫秒: Application.DoEvents()   // 强制等待一帧
    RefreshStrings(true)
    重新开始计时
```

## 备注

- **文档与源码的差异**：ecd 文档称强制等待为「0.2 秒」；源码实际等待的是一帧时间 `msPerFrame = 1000 / Config.FPS`（默认 60FPS，约 16.7ms，`UI/Game/EmueraConsole.cs:1537`）。文档的 0.2 秒与实现不符（疑为旧版 Emuera 行为或文档笔误）。
- 与 `SETCOLOR` 的实现差异：`SETBGCOLOR` 分支额外处理 `colorArg.IsConst`（常量参数直接取 `ConstInt` 按 RGB 单值拆位），`SETCOLOR` 分支没有该路径。
- `zh/` 文档套件未收录本命令，无从交叉核对。
