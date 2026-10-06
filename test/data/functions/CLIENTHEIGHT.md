# CLIENTHEIGHT

- **类别**：式中函数（EE 扩展函数）
- **签名**：`int CLIENTHEIGHT()`
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 无独立小节，仅在「MOUSEY」小节提及（「客户端区域的大小可以用 `CLIENTWIDTH`、`CLIENTHEIGHT` 函数获取」）；zh 套件未收录

## 语义

返回 Emuera 客户端区域（窗口内文字/图像绘制区，即主 PictureBox）当前的高度，单位为像素。无参数、无副作用。

典型用途：`MOUSEY` 的坐标以客户端区域左下角为 `(0,0)`、向上为负，若要以左上角为基准的 Y 坐标，可用 `MOUSEY() + CLIENTHEIGHT()` 换算。

## 用法

### int CLIENTHEIGHT()
- 无参数。
```erb
PRINTFORML 客户端区域：{CLIENTWIDTH()} x {CLIENTHEIGHT()} 像素
; 鼠标 Y 换算为以左上角为原点的坐标
PRINTFORML 鼠标 Y（左上原点）= {MOUSEY() + CLIENTHEIGHT()}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:159`（`["CLIENTHEIGHT"] = new ClientSizeMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5916`（`ClientSizeMethod`，返回 long，无参，`CanRestructure = false`）；属性来源 `UI/Game/EmueraConsole.cs:255`（`ClientHeight => window.MainPicBox.Height`）

```text
函数 CLIENTHEIGHT():
    按 Name 分派（与 CLIENTWIDTH 共用同一个 ClientSizeMethod 类）:
        Name == "CLIENTHEIGHT" → 返回 exm.Console.ClientHeight
        （即 window.MainPicBox.Height，主绘图控件的像素高度）
    其他 Name → 抛出 ExeEE（内部异常："ClientSize:异常な分岐"）
```

## 备注

- `CanRestructure = false`：窗口大小可随时改变，结果不作为定值缓存，每次求值实时取得。
- 与 `CLIENTWIDTH` 共用同一个实现类，靠函数名区分分支；这是源码中少见的按 `Name` 分派的写法。
- 三套文档均无独立小节，语义（含鼠标坐标换算示例）来自 `ecd/Command.md` 的 MOUSEY 小节与源码。
