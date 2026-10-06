# MOVETEXTBOX

- **类别**：式中函数（EE 扩展 / 输入框布局）
- **签名**：`int MOVETEXTBOX(int X偏移, int Y偏移, int 宽度)`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）、EE readme、EM readme 均未收录本函数；同族 `GETTEXTBOX`/`SETTEXTBOX` 的既有文档（`test/data/commands/GETTEXTBOX.md`、`test/data/commands/SETTEXTBOX.md`）提到 EM+EE 在线文档的 TEXTBOX 页把 `MOVETEXTBOX`/`RESUMETEXTBOX` 与之同页收录，但该页不在本仓库的提取材料中。本文语义据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:1969` 的 `MoveTextBoxMethod` 与 `UI/Framework/Forms/MainWindow.cs:181` 的 `SetTextBoxPos`）。

## 语义

移动输入框（文本框）的位置并改变其宽度。三个参数依次是**相对屏幕左下角的 X 偏移、相对底边的 Y 偏移、宽度**：

- `X偏移`：输入框左边缘距客户区左边缘的像素数（内部 `Math.Max(0, Math.Min(xOffset, ClientSize.Width - 50))` 夹取，即最少留 50 像素可见）。
- `Y偏移`：输入框底边距客户区**底边**的像素数（内部换算为 `Top = ClientSize.Height - yOffset - 高度`，并把结果夹在 `0 .. ClientSize.Height - 高度`）。
- `宽度`：新的输入框宽度（内部夹取为至少 50，最多不超过客户区右边界）。高度不变（沿用当前输入框高度）。

返回值恒为 `1`。它只改变输入框的摆放，不影响显示行（`PRINT` 输出）与输入等待的行为。要恢复默认位置用 `RESUMETEXTBOX`。

## 用法

### int MOVETEXTBOX(X偏移, Y偏移, 宽度)
- X偏移：相对左边缘的像素偏移（负数按 0 处理）。
- Y偏移：相对**底边**的像素偏移（越大越往上）。
- 宽度：输入框宽度（像素）。
- 返回值：恒 `1`。
```erb
; 把输入框挪到画面底部偏上、加宽到 600 像素
MOVETEXTBOX 20, 40, 600

; 输入框贴底、占满大部分宽度
MOVETEXTBOX 0, 0, 640
```

```erb
; 配合 GETTEXTBOX/SETTEXTBOX 做输入区重排
SETTEXTBOX("请选择：")
MOVETEXTBOX 10, 30, 500
INPUT
RESUMETEXTBOX 0, 0, 0     ; 恢复默认位置（参数被忽略）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:308`（`["MOVETEXTBOX"] = new MoveTextBoxMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1969`（`MoveTextBoxMethod`，构造参数 `b = false`）→ `UI/Framework/Forms/MainWindow.cs:181`（`SetTextBoxPos(int, int, int)`）

```text
MoveTextBoxMethod:
    构造(b = false):
        ReturnType = long
        argumentTypeArray = [long, long, long]      # 恰好 3 个整数参数（对 RESUMETEXTBOX 同样如此）
        CanRestructure = false
        resume = b                                  # RESUMETEXTBOX 注册时传 true
    GetIntValue(exm, arguments):
        若 resume:                                   # RESUMETEXTBOX 分支
            exm.Console.Window.ResetTextBoxPos()     # 三个参数被完全忽略
        否则:                                        # MOVETEXTBOX 分支
            exm.Console.Window.SetTextBoxPos((int)arguments[0], (int)arguments[1], (int)arguments[2])
        返回 1

MainWindow.SetTextBoxPos(xOffset, yOffset, width)（MainWindow.cs:181）:
    nextTextBoxInfo.Left = Math.Max(0, Math.Min(xOffset, ClientSize.Width - 50))
    nextTextBoxInfo.Top  = Math.Min(Math.Max(ClientSize.Height - yOffset - richTextBox1.Height, 0),
                                    ClientSize.Height - richTextBox1.Height)
    nextTextBoxInfo.Size = new Size(Math.Max(50, Math.Min(width, ClientSize.Width - richTextBox1.Left)),
                                    richTextBox1.Size.Height)      # 高度沿用现行值
    textBoxState = TextBoxState.WatingToChange               # 只登记「待变更」，不立即搬动控件

MainWindow.ApplyTextBoxChanges():
    若 状态是 WatingToChange 或 ScrollBack:
        SetTextBoxPos(nextTextBoxInfo)      # 真正写入 richTextBox1 的 Left/Top/Size
        textBoxState = TextBoxState.Changed
```

## 备注

- 语义据源码；该函数的三个参数含义（左下角基准、像素单位、夹取下限 50）在提取材料中无任何文档可核对，全部据源码，其中「Y偏移以底边为基准、越大越靠上」由 `Top = ClientSize.Height - yOffset - 高度` 推得，属**推定**（与 `SETTEXTBOX` 系列同属 `EM_textbox位置指定拡張`）。
- **不是立即生效**：`SetTextBoxPos` 只把新位置登记到 `nextTextBoxInfo` 并置状态为「待变更」，实际搬动控件发生在 `ApplyTextBoxChanges`（由滚动条位置变化等时机触发，`UI/Framework/Forms/MainWindow.cs:198`）。因此连续调用 `MOVETEXTBOX` 只有最后一次的参数会留下。
- 滚动条行为：`textBoxHandleScrollValueChanged`（`UI/Framework/Forms/MainWindow.cs:160`）在文本框可滚动时会把 `ResetTextBoxPos`/`ScrollBackTextBoxPos` 与 `ApplyTextBoxChanges` 交替使用，所以移动后的位置在「滚回底部」时才会最终应用。
- 返回值恒 `1`，没有失败分支；参数越界不会报错，只被 `Math.Min/Max` 夹取。
- 高度无法通过本函数修改（`Size` 的高度恒取 `richTextBox1.Size.Height`）；参数个数/类型不符会在解析期报错。
- 函数形态而非命令形态：EE readme/EM+EE 发行版可能把 `MOVETEXTBOX` 当命令收录（关键字帮助），本仓库的注册在 `methodList`，因此必须写成 `MOVETEXTBOX(20, 40, 600)` 这样的函数调用形式（与 `SETTEXTBOX` 的情况相同，参见 `test/data/commands/SETTEXTBOX.md` 备注）。
