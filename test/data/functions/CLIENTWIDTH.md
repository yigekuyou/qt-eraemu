# CLIENTWIDTH

- **类别**：式中函数（EE 扩展函数）
- **签名**：`int CLIENTWIDTH()`
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 无独立小节，仅在「MOUSEY」小节提及（「客户端区域的大小可以用 `CLIENTWIDTH`、`CLIENTHEIGHT` 函数获取」）；zh 套件未收录

## 语义

返回 Emuera 客户端区域（窗口内文字/图像绘制区，即主 PictureBox）当前的宽度，单位为像素。无参数、无副作用。

常与 `CLIENTHEIGHT`、`MOUSEX`/`MOUSEY` 配合做鼠标坐标换算或布局计算。

## 用法

### int CLIENTWIDTH()
- 无参数。
```erb
PRINTFORML 客户端区域：{CLIENTWIDTH()} x {CLIENTHEIGHT()} 像素
; 鼠标 X 本就以左下角为原点、右为正，无需换算
IF MOUSEX() >= 0 && MOUSEX() < CLIENTWIDTH()
    PRINTL 鼠标在客户端区域内
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:158`（`["CLIENTWIDTH"] = new ClientSizeMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5916`（`ClientSizeMethod`，返回 long，无参，`CanRestructure = false`）；属性来源 `UI/Game/EmueraConsole.cs:254`（`ClientWidth => window.MainPicBox.Width`）

```text
函数 CLIENTWIDTH():
    按 Name 分派（与 CLIENTHEIGHT 共用同一个 ClientSizeMethod 类）:
        Name == "CLIENTWIDTH" → 返回 exm.Console.ClientWidth
        （即 window.MainPicBox.Width，主绘图控件的像素宽度）
    其他 Name → 抛出 ExeEE（内部异常："ClientSize:异常な分岐"）
```

## 备注

- `CanRestructure = false`：窗口大小可随时改变，结果不缓存，每次求值实时取得。
- 与 `CLIENTHEIGHT` 共用同一个实现类，靠函数名区分分支。
- 三套文档均无独立小节，语义来自 `ecd/Command.md` 的 MOUSEY 小节与源码。
