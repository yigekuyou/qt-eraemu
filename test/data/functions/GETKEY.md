# GETKEY

- **类别**：式中函数
- **签名**：int GETKEY(int keycode)
- **文档来源**：`ecd/Command.md`「### GETKEY `<键码>`」；`ecd/Expression.md` 未收录

## 语义

返回键盘及鼠标按键的实时状态：参数指定的键（虚拟键码）当前被按下时返回 `1`，否则返回 `0`。

该函数只在 Emuera 窗口处于活动状态时才可能返回 `1`；窗口非活动时无论按键状态如何都返回 `0`。

键码数值与实际按键的对应关系参考微软 MSDN 中 `GetKeyState()` 一节（Win32 虚拟键码，0～255）。

注意与同名概念区分：`GETKEYTRIGGERED` 是"刚按下的瞬间"，`GETKEY` 是"当前是否按住"。

## 用法

### int GETKEY(int keycode)
- `keycode`：虚拟键码（0～255）。超出范围时直接返回 0。
- 返回值：按下为 1，否则 0。
```erb
;轮询等待：A 键（键码 65）按下时继续
WHILE GETKEY(65) == 0
    AWAIT
WEND
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:161`（`["GETKEY"] = new GetKeyStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6784`（`GetKeyStateMethod`，与 `GETKEYTRIGGERED` 共用）；P/Invoke 声明 `Runtime/Utils/WinInput.cs:6`

```text
静态字段：static readonly short[] keytoggle = new short[256];    ; 记录每个键上次调用的低位状态

构造：返回类型 = long；参数 = [long]；CanRestructure = false。

GetIntValue(exm, args):
    若 !exm.Console.IsActive:            ; 窗口非活动
        返回 0
    keycode ← args[0].GetIntValue(exm)
    若 keycode < 0 或 keycode > 255:
        返回 0
    s ← WinInput.GetKeyState((int)keycode)      ; Win32 GetKeyState：高位(负值)=按下，低位=toggle
    toggle ← keytoggle[keycode]
    keytoggle[keycode] ← (short)((s & 1) + 1)   ; 初始 0，之后按 toggle 位存 1 或 2
    按 Name 分支（本函数 Name = "GETKEY"）:
        GETKEY:           返回 (s < 0) ? 1 : 0
        GETKEYTRIGGERED:  （见 GETKEYTRIGGERED.md）
```

## 备注

- ecd/Command.md 以「赋值给 `RESULT:0`」的命令式口吻描述；实际注册形态是式中函数。
- 实现 Win32 `GetKeyState`：返回的 short 最高位（值为负）表示按下状态，与文档描述一致。
- 副作用：每次调用（窗口活动且键码有效时）都会更新 `keytoggle` 静态数组；`GETKEY` 与 `GETKEYTRIGGERED` 共享这一状态，因此混用两者会影响 `GETKEYTRIGGERED` 的判定（详见 GETKEYTRIGGERED.md 备注）。
- zh 套件（Era-Chinese-Documentation）未收录本函数。
