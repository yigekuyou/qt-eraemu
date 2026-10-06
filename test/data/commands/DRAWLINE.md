# DRAWLINE

- **类别**：命令
- **签名**：
  - `DRAWLINE`
- **文档来源**：`ecd/docs/translation/Command.md` **无 DRAWLINE 独立小节**（`ecd/ERB_Commands.md:406` 有命令一览条目「`DRAWLINE` 绘制分隔线」）；`Era-Chinese-Documentation/docs/ERB_File_Format.md`「其他基本命令」小节（「用`--`画一条从左至右的分割线」，带示例）。

## 语义

无参数。用 `_Replace.csv` 中 `DRAWLINE字符`（`DRAWLINE character`，默认 `-`）指定的字符填满一整行——即从画面左端到右端画一条分割线（默认效果为一整行 `----…`），然后换行。

所用字符由配置项 `DrawLineString`（`_Replace.csv` 的 `DRAWLINE文字`，默认 `-`）决定，运行时字符串变量 `DRAWLINESTR` 中记录的就是执行 `DRAWLINE` 时显示的字符串。线条以常规字体（非粗斜体等）绘制，字体样式固定为 Regular。受打印跳过（`SKIPDISP`/输入跳过）影响：跳过打印时本命令什么也不显示。

## 用法

### `DRAWLINE`
- 无参数。
```erb
PRINTL 物品清单
DRAWLINE
PRINTL ＝＝＝＝＝＝＝＝＝＝
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:188`（`argb[FunctionArgType.VOID]`，flag = `METHOD_SAFE`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:56`）
- 实现：switch-case 分发于 `Runtime/Script/Process.ScriptProc.cs:156`；绘制在 `UI/Game/EmueraConsole.Print.cs:692`（`PrintBar()`），线条字符预生成于 `UI/Game/EmueraConsole.Print.cs:726`（`getStBar()`）

```text
ScriptProc 执行期 case FunctionCode.DRAWLINE:
    若 skipPrint: break              # 处于打印跳过状态时不输出
    exm.Console.PrintBar()
    exm.Console.NewLine()            # 画完一行后换行

PrintBar():
    ss = userStyle
    userStyle.FontStyle = FontStyle.Regular   # 字体样式强制为常规
    Print(stBar)                     # stBar 为启动时按 DRAWLINE字符 预生成的整行字符串
    userStyle = ss

getStBar(barStr):                    # 生成整行线条
    builder = barStr
    width = 0
    当 width < Config.DrawableWidth:  # 逐字符追加，直到显示宽度达到画面宽度
        builder.Append(barStr)
        width = 显示宽度(builder)
    当 width > Config.DrawableWidth:  # 超出时逐字符回退（支持多字符 barStr）
        去掉最后一个字符并重测
    return builder

# stBar 的字符来源：Config.DrawLineString（_Replace.csv 的 "DRAWLINE文字"/"DRAWLINE character"，默认 "-"）
```

## 备注

- `DRAWLINE`（本命令）与 `DRAWLINEFORM`、`CUSTOMDRAWLINE` 的区别：`DRAWLINE` 用固定的配置字符且在启动时预生成整行；`DRAWLINEFORM` 每次按 FORM 参数动态生成；`CUSTOMDRAWLINE` 用常量参数动态生成（与 `DRAWLINEFORM` 共用指令处理）。
- 源码注释说明 `CompatiDRAWLINE` 兼容选项已废除（1806beta001），相关换行行为移交给 `CompatiLinefeedAs1739` 的 `PrintStringBuffer` 处理。
- zh 套件的示例显示默认效果为一整行 `--…`；`_Replace.csv` 中 `DRAWLINE字符` 未设置时源码默认值同样是 `-`，两套文档与源码一致。
