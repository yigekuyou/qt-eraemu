# MOUSEB

- **类别**：式中函数（EE 扩展 / 鼠标相关）
- **签名**：`str MOUSEB()`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）未收录；EE readme `eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt:185-187` 有记载（「・MOUSEB 現在マウスオーバー中のボタン内容を取得する。MOUSEX,MOUSEYと同様にAWAITと組み合わせて使う / 実行時点でINPUTかINPUTSか確定していないため文字列型として返される点に注意」），`EmueraEE_changelog.txt:80`「MOUSEB追加」。语义以源码为准（本仓库 `emuera.em/Readme/EmueraEE_readme.txt` 版本未含此条）。

## 语义

取得**当前鼠标指针下的按钮内容**（按钮上显示的输入值）。与 `MOUSEX`/`MOUSEY` 一样，需要在 `AWAIT` 待机中才有意义——`AWAIT` 期间引擎会持续跟踪鼠标位置，因此推荐写法是在 `AWAIT` 循环里调用本函数。

返回类型是**字符串**：因为按钮可能是整数按钮（`INPUT` 用）也可能是字符串按钮（`INPUTS` 用），在调用时点无法确定，所以统一按字符串返回（EE readme 明确提示了这点）。实现上：

- 指针下没有按钮、或指针所在按钮不是输入按钮 → 返回空串。
- 指针下有按钮且是整数按钮 → 返回按钮整数值的十进制字符串。
- 指针下有按钮且是字符串按钮 → 返回按钮的字符串值。

副作用：为了让待机中的鼠标状态即时生效，函数内部会临时把「总是刷新」开关置为真、调用 `EmueraConsole.MoveMouse` 更新指针状态，然后把开关还原。它不消耗鼠标事件、不改变输入等待的结果（不会替玩家按下按钮）。

CBG 按钮贴图（`CBGSETBMAP*`）的情况由 `MoveMouse` 内部的 CBG 判定处理：此时指针下命中的是 CBG 按钮编号，而**不是**普通文本按钮，因此 `MOUSEB` 会返回空串（`pointingString` 被清空）。

## 用法

### str MOUSEB()
- 无参数。
- 返回值：指针下按钮的内容（整数值时为其十进制字符串）；无按钮时为空串。
```erb
; 在待机中显示鼠标下按钮的内容（EE readme 推荐的 AWAIT 组合用法）
$MOUSE_WATCH
AWAIT 1
S = MOUSEB()
PRINTL S
GOTO MOUSE_WATCH
```

```erb
; 把指针下的按钮内容当作提示信息
$SHOW_HINT
AWAIT 1
S = MOUSEB()
IF S != ""
    SETCOLOR 0xFFFF00
    PRINTL "指向：" + S
    RESETCOLOR
ENDIF
GOTO SHOW_HINT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:166`（`["MOUSEB"] = new MouseButtonMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6830`（`MouseButtonMethod`）→ `UI/Game/EmueraConsole.cs:599`（`PointingSring` 属性）、`UI/Game/EmueraConsole.cs:2219`（`MoveMouse`）、`UI/Game/ConsoleButtonString.cs:120-123`（`IsButton`/`IsInteger`/`Input`/`Inputs`）

```text
MouseButtonMethod:
    构造:
        ReturnType = string
        argumentTypeArray = []              # 无参数
        CanRestructure = false
    GetStrValue(exm, arguments):
        b = exm.Console.AlwaysRefresh                     # 保存当前刷新开关
        point = exm.Console.Window.MainPicBox.PointToClient(Control.MousePosition)
                                                          # 屏幕坐标 → 主绘图区客户区坐标
        exm.Console.AlwaysRefresh = true                  # 强制刷新，使指针状态即时更新
        若 MainPicBox.ClientRectangle 包含 point:
            exm.Console.MoveMouse(point)                  # 更新 pointingString / pointingStrings / selectingCBGButtonInt
        exm.Console.AlwaysRefresh = b                     # 还原开关
        若 exm.Console.PointingSring != null:             # 指针下存在按钮字符串
            若 !PointingSring.IsButton: 返回 ""            # 非输入按钮（纯文本）→ 空串
            若 PointingSring.IsInteger: 返回 PointingSring.Input.ToString()
            否则: 返回 PointingSring.Inputs
        返回 ""                                           # 指针不在窗口内 / 不在任何按钮上 / CBG 按钮命中

EmueraConsole.PointingSring（:599）:
    get { return pointingString; }

ConsoleButtonString（:38-95 的构造函数）决定四个字段:
    整数按钮:  Input = value; Inputs = value.ToString(); IsButton = true; IsInteger = true
    字符串按钮: Inputs = value;                            IsButton = true; IsInteger = false
```

## 备注

- 语义据源码；EE readme 的说明（「取得鼠标悬停中按钮的内容」「与 AWAIT 组合」「返回字符串类型」）与源码一致。本仓库 `emuera.em/Readme/EmueraEE_readme.txt` 未收录该条目，本文引用的 readme 出处是 `eraTW/README集/EmueraEE Readme/` 下的同文件副本。
- 需要 `INPUT`/`INPUTS` 族生成的按钮才返回内容：纯文本行的 `PointingSring` 存在但 `IsButton = false`，返回空串。
- 与 `MOUSEX`/`MOUSEY` 的差异：后两者返回坐标（整数），本函数返回按钮内容（字符串）；三者都依赖 `AWAIT` 期间的指针跟踪。
- 本函数在**非 Windows/Forms 前端**下没有意义（依赖 `Control.MousePosition` 与 `MainPicBox`）；源码未做平台判定。
- 临时改写 `AlwaysRefresh` 是源码里唯一的副作用；它只影响本次调用的刷新行为，调用结束后恢复原值。
- CBG 按钮贴图命中时 `MoveMouse` 会走 CBG 分支并清空 `pointingString`（`UI/Game/EmueraConsole.cs:2245-2250`），所以 `MOUSEB` 返回空串——即它只反映**文本按钮**的内容。
