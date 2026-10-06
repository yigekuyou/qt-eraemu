# GETKEYTRIGGERED

- **类别**：式中函数
- **签名**：int GETKEYTRIGGERED(int keycode)
- **文档来源**：`ecd/Command.md`「### GETKEYTRIGGERED `<键码>`」；`ecd/Expression.md` 未收录

## 语义

与 `GETKEY` 一样返回键盘及鼠标按键的状态，但语义不同：`GETKEY` 获取的是当前是否按下，而 `GETKEYTRIGGERED` 只在"刚按下的瞬间"返回 `1`。也就是说，持续按住时 `GETKEY` 一直返回 `1`，而 `GETKEYTRIGGERED` 只在最初返回 `1`，之后返回 `0`。

该函数只在 Emuera 窗口处于活动状态时有效；窗口非活动时无论按键状态如何都返回 `0`。

键码与 MSDN `GetKeyState()` 的虚拟键码对应（0～255）。

## 用法

### int GETKEYTRIGGERED(int keycode)
- `keycode`：虚拟键码（0～255）。超出范围时返回 0。
- 返回值：本次调用检测到"刚按下"时返回 1，否则 0。
```erb
;在主循环中检测 A 键（键码 65）刚被按下的瞬间
IF GETKEYTRIGGERED(65) != 0
    PRINTL A 键刚被按下
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:162`（`["GETKEYTRIGGERED"] = new GetKeyStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6784`（`GetKeyStateMethod`，与 `GETKEY` 共用同一个类，按注册名分支）

```text
静态字段：static readonly short[] keytoggle = new short[256];    ; 与 GETKEY 共享

构造：返回类型 = long；参数 = [long]；CanRestructure = false。

GetIntValue(exm, args):
    若 !exm.Console.IsActive:
        返回 0
    keycode ← args[0].GetIntValue(exm)
    若 keycode < 0 或 keycode > 255:
        返回 0
    s ← WinInput.GetKeyState((int)keycode)
    toggle ← keytoggle[keycode]                  ; 上次调用时存的值
    keytoggle[keycode] ← (short)((s & 1) + 1)    ; 由本次 GetKeyState 的低位决定，存 1 或 2
    按 Name 分支（本函数 Name = "GETKEYTRIGGERED"）:
        返回 (s < 0 且 toggle != keytoggle[keycode]) ? 1 : 0
        ; 即：键被按下，且 GetKeyState 的低位（toggle 位）与上次调用相比发生了变化
```

## 备注

- ecd/Command.md 以「赋值给 `RESULT:0`」的命令式口吻描述；实际注册形态是式中函数。
- **文档与实现存在差异**：文档描述为"只在刚按下的瞬间返回 1"（即按下沿触发）；但实现判断的是 `GetKeyState` 返回值的低位（toggle 位，只有 NumLock/CapsLock 等切换键才会变化）与上次记录值是否不同，且该记录在键未按下时也会被更新。对于普通的非切换键，低位恒为 0，`keytoggle` 恒存 1，因此实际效果是：只有该键第一次被查询且正处于按下状态时返回 1，此后即使松开再按也返回 0（源码注释自述为「初回は true、2 回目以降は toggle 状態が前回と違う場合のみ 1」）。这与文档"每次新按下都触发"的语义不一致，疑似实现缺陷，使用时应以实现行为为准。
- `GETKEY` 与本函数共用同一个类和同一个 `keytoggle` 状态数组：任何一方的调用都会刷新记录，混用会影响本函数的判定。
- zh 套件（Era-Chinese-Documentation）未收录本函数。
