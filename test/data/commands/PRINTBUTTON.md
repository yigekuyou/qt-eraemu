# PRINTBUTTON

- **类别**：命令
- **签名**：
  - `PRINTBUTTON <字符串表达式>, <数值表达式或字符串表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「按钮」小节（`### PRINTBUTTON(|C|LC)`）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

生成可以用鼠标点击的按钮。格式与 `PRINTS` 接近，但第 2 参数指定了单击该按钮时作为输入送入的数值或字符串（按钮点击的效果等价于用户手动输入该值后回车，因此数值按钮配 `INPUT`、字符串按钮配 `INPUTS` 使用）。

Emuera 通常会把 `[300] 存档` 这种用 `[]` 封闭数字后跟文字的文本在绘制时自动转换成按钮，但自动识别并不总是正确（例如一行里有两处时）；`PRINTBUTTON` 用来跳过自动识别、强制把指定文本按钮化。第 1 参数中的换行符会被忽略。

第 1 参数显示的文本并不需要包含 `[0]` 之类的标注，但仍建议标注以提示玩家这是按钮。

## 用法

### `PRINTBUTTON <字符串表达式>, <数值表达式或字符串表达式>`
- `<字符串表达式>`：按钮上显示的文本（换行符被忽略）。
- `<数值表达式或字符串表达式>`：点击按钮时送入的输入值；数值时配 `INPUT`，字符串时配 `INPUTS`。
```erb
;修正自动按钮化的误判
PRINTS "是要这样么？ "
PRINTBUTTON "[0] 是", 0
PRINTS "    "
PRINTBUTTON "[1] 否", 1
INPUT

;字符串按钮
PRINTL 请输入名字
PRINTBUTTON "[穗月]", "穗月"
PRINTBUTTON "[美穗]", "美穗"
PRINTBUTTON "[其他]", "其他"
INPUTS
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:173`（`argb[FunctionArgType.SP_BUTTON]`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:111`（`case FunctionCode.PRINTBUTTON` 分支，`doNormalFunction` 内）；按钮节点构造 `UI/Game/EmueraConsole.Print.cs:596`（`PrintButton(string, string)` / `PrintButton(string, long)`）

```text
case PRINTBUTTON:
    若 skipPrint: break                          # SKIPDISP 1 时整个跳过
    exm.Console.UseUserStyle = true              # 使用用户样式
    exm.Console.UseSetColorStyle = true
    bArg = (SpButtonArgument)func.Argument       # (显示文本, 按钮输入值)
    str = bArg.PrintStrTerm 求字符串值
    str = str.Replace("\n", "")                  # 换行符全部删除（避免按钮绘制错乱）
    若 bArg.ButtonWord 的操作数类型是 long:
        exm.Console.PrintButton(str, bArg.ButtonWord 求整数值)
    否则:
        exm.Console.PrintButton(str, bArg.ButtonWord 求字符串值)

Console.PrintButton(str, p):
    若 str 为空或 null: return                   # 空文本不生成按钮
    printBuffer.AppendButton(str, Style, p)      # 向行缓冲追加一个按钮节点
```

## 备注

- `PRINTBUTTONC` / `PRINTBUTTONLC` 是本命令带对齐的变体，分别有独立文档（见 `PRINTBUTTONC.md`、`PRINTBUTTONLC.md`）。
- 「换行符会被忽略」在源码中是显式的 `Replace("\n", "")`，注释说明原因是「按钮处理会因此显示错乱，故 PRINTBUTTON 中省略换行码」。
- 按钮被点击后送入的值与 `INPUT`/`INPUTS` 的联动（含按钮世代 `lastButtonGeneration` 机制）在 `UI/Game/EmueraConsole.cs` 的输入处理中实现，不在本命令的执行范围内。
- zh 套件未收录本命令。
