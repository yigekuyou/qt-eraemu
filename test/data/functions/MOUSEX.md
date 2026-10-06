# MOUSEX

- **类别**：式中函数
- **签名**：
  - `int MOUSEX()`
- **文档来源**：`ecd/Command.md`「AWAIT 相关」→「MOUSEX」小节；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

获取鼠标光标当前的 X 坐标。坐标以客户端区域左下角为原点 `(0,0)`，右方向为 x 轴正方向。鼠标在客户端区域外时也正常动作（返回按同一坐标系折算的坐标）。窗口未创建时返回 `0`。该函数无副作用、无参数，常与 `MOUSEY`、`CLIENTWIDTH`/`CLIENTHEIGHT` 配合做鼠标命中判断。

## 用法

### MOUSEX()

- 无参数，`()` 不可省略。

```erb
X = MOUSEX()
Y = MOUSEY()
IF X >= 0 && X < CLIENTWIDTH() && Y >= -CLIENTHEIGHT() && Y < 0
	; 光标位于客户端区域内
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:163`（`["MOUSEX"] = new MousePosMethod()`；`:164` 为 `MOUSEY`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6811`（`MousePosMethod`；坐标源 `UI/Game/EmueraConsole.cs:2197` `GetMousePosition`）

```text
function MOUSEX(args, exm):
    switch Name:
        case "MOUSEX": return Console.GetMousePosition().X
        case "MOUSEY": return Console.GetMousePosition().Y
    ; MousePosMethod 同类实现，按注册名分流

function Console.GetMousePosition():   ; EmueraConsole.cs:2197
    if window == null or not window.Created:
        return Point(0, 0)
    pos = window.MainPicBox.PointToClient(Cursor.Position)  ; 屏幕坐标 → 客户区坐标
    pos.Y -= ClientHeight                                    ; 原点从左上移到左下
    return pos
```

## 备注

- `MousePosMethod` 同时服务 `MOUSEX` 与 `MOUSEY`，运行时按函数名 `switch` 分流；若函数名异常抛出内部错误 `ExeEE`。
- 注意 Y 轴方向：原点在客户端区域左下角、向下为正，因此光标在客户端区域内时 `MOUSEY()` 为负值（见 `MOUSEY.md`）；`MOUSEX` 无此符号问题。
- `CanRestructure = false`：每次求值都实时读取光标位置。
