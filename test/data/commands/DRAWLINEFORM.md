# DRAWLINEFORM

- **类别**：命令（EE 扩展命令）
- **签名**：
  - `DRAWLINEFORM <FORM格式文本>`
- **文档来源**：`ecd/docs/translation/Command.md`「PRINT系列」小节（「使用指定的文本填满一整行。DRAWLINEFORM是支持FORM格式文本的版本」）；zh 套件未收录本命令。

## 语义

使用参数指定的文本（先展开 FORM 语法）填满一整行——相当于「可指定线条字符的 `DRAWLINE`」。把参数字符串反复拼接，直到其显示宽度达到画面宽度（`DrawableWidth`），超出时回退到刚好不超出的长度，然后作为一行输出并换行。

参数为空字符串时抛出 CodeEE 错误。线条以常规字体绘制。受打印跳过影响：跳过打印时不输出。

ecd 文档中 `DRAWLINEFORM` 与 `CUSTOMDRAWLINE` 在同一小节描述；两者的区别在于参数类型（`CUSTOMDRAWLINE` 接受常量文本，`DRAWLINEFORM` 接受 FORM 格式文本）。

## 用法

### `DRAWLINEFORM <FORM格式文本>`
- `<FORM格式文本>`：先展开 FORM 语法的文本；该文本会被重复铺满一整行。
```erb
DRAWLINEFORM -=-
;输出： -=-=--=-=--=-=--=-=--…（铺满一行）
DRAWLINEFORM %{BARSTR}%
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:287`（`argb[FunctionArgType.FORM_STR]`，flag = `METHOD_SAFE | EXTENDED`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:187`）
- 实现：switch-case 分发于 `Runtime/Script/Process.ScriptProc.cs:163`；绘制在 `UI/Game/EmueraConsole.Print.cs:708`（`printCustomBar()`），铺满逻辑在 `UI/Game/EmueraConsole.Print.cs:726`（`getStBar()`）

```text
ScriptProc 执行期 case FunctionCode.DRAWLINEFORM:
    若 skipPrint: break
    term = 参数表达式（FORM 展开）
    str = term.GetStrValue(exm)
    exm.Console.printCustomBar(str, isConst=false)
    # 注：其上的 CUSTOMDRAWLINE case 源码中已被注释掉（该命令改由 CUSTOMDRAWLINE_Instruction 独立处理）；本 case 自行完成换行

printCustomBar(barStr, isConst):
    若 barStr 为 null 或空串:
        抛出 CodeEE（"线条字符串为空"）
    ss = userStyle
    userStyle.FontStyle = FontStyle.Regular     # 字体样式强制为常规
    若 isConst: Print(barStr)                   # CUSTOMDRAWLINE 用常量原样输出
    否则:       Print(getStBar(barStr))         # 本命令：按画面宽度铺满
    userStyle = ss

getStBar(barStr):
    builder = barStr
    width = 0
    当 width < Config.DrawableWidth:            # 反复追加整串，直到达到画面显示宽度
        builder.Append(barStr)
        width = stringMeasure.GetDisplayLength(builder, Config.DefaultFont)
    当 width > Config.DrawableWidth:            # 超出则逐字符回退（兼容多字符 barStr）
        builder.Remove(末尾1字符)
        width = 重新测量
    return builder
```

## 备注

- 源码中 `DRAWLINEFORM` 与 `CUSTOMDRAWLINE` 最终都调用 `UI/Game/EmueraConsole.Print.cs` 的 `printCustomBar`：`CUSTOMDRAWLINE` 走 `isConst=true`（常量原样 `Print`，不重复铺满），`DRAWLINEFORM` 走 `isConst=false`（`getStBar` 铺满整行）——与 ecd 文档「使用指定的文本填满一整行」一致。
- 注意与 `DRAWLINESTR`（变量）不同：`DRAWLINESTR` 记录的是 `DRAWLINE` 指令使用的字符串，不会因 `DRAWLINEFORM` 而改变。
- 空参数报错的行为文档未提及，为源码补充。
- zh 套件未收录本命令。
