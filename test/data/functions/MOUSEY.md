# MOUSEY

- **类别**：式中函数
- **签名**：
  - `int MOUSEY()`
- **文档来源**：`ecd/Command.md`「AWAIT 相关」→「MOUSEY」小节；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

获取鼠标光标当前的 Y 坐标。坐标以客户端区域左下角为原点 `(0,0)`，下方向为 y 轴正方向——因此光标在客户端区域内时返回**负值**，这是最容易踩的坑。需要以客户端区域左上角为基准的 Y 坐标时用 `MOUSEY() + CLIENTHEIGHT()`。客户端区域大小可用 `CLIENTWIDTH`、`CLIENTHEIGHT` 函数获取；鼠标在客户端区域外时也正常动作，窗口未创建时返回 `0`。

## 用法

### MOUSEY()

- 无参数，`()` 不可省略。

```erb
; 光标是否在客户端区域内（Y 需满足 -CLIENTHEIGHT() <= Y < 0）
IF MOUSEY() >= -CLIENTHEIGHT() && MOUSEY() < 0
	PRINTL 光标在窗口内
ENDIF
; 换算成常规的"左上原点向下为正"坐标
TOP_Y = MOUSEY() + CLIENTHEIGHT()
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:164`（`["MOUSEY"] = new MousePosMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6811`（`MousePosMethod`；坐标源 `UI/Game/EmueraConsole.cs:2197` `GetMousePosition`）

```text
function MOUSEY(args, exm):
    return Console.GetMousePosition().Y   ; 同类 MousePosMethod 按名分流

function Console.GetMousePosition():      ; EmueraConsole.cs:2197
    if window == null or not window.Created:
        return Point(0, 0)
    pos = window.MainPicBox.PointToClient(Cursor.Position)
    pos.Y -= ClientHeight    ; 关键一步：把左上原点坐标换成左下原点坐标
    return pos
```

## 备注

- 文档明确警告"光标在客户端区域内时 MOUSEY 返回负值"，源码 `pos.Y -= ClientHeight` 与之完全对应；窗口未创建时 `GetMousePosition` 返回 `(0,0)`（文档未提）。
- 与 `MOUSEX` 共用 `MousePosMethod`（`Runtime/Script/Statements/Function/Creator.Method.cs:6811`），无参函数，`CanRestructure = false`。
