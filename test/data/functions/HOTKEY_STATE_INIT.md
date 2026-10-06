# HOTKEY_STATE_INIT

- **类别**：式中函数（EE 扩展 / 热键系统）
- **签名**：`int HOTKEY_STATE_INIT(int 状态数)`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）与 EE readme 均未收录本函数；语义据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:7609` 的 `HotkeyStateInitMethod`）与 `UI/Game/HotkeyState.cs:1-38` 的说明注释、`:48` 的 `HotkeyStateInit`。

## 语义

建立热键系统的状态数组，参数为数组长度（要保存多少个状态值）。按源码注释的推荐用法，应在标题画面（title）里、任何 `HOTKEY_STATE` 调用之前执行一次。内部实现为 `state = new nint[size]`，即分配新数组——**重复调用会丢弃旧状态**（不是「只在未初始化时生效」）。

返回值恒为 `0`。它只负责分配状态数组，不开启热键功能；热键功能仍需 `HOTKEY.ERB` 文件并手动按 Ctrl+D 切换（`HotkeyState.Toggle`）。

## 用法

### int HOTKEY_STATE_INIT(状态数)
- 状态数：状态数组的元素个数；之后 `HOTKEY_STATE` 的索引必须落在 `0..状态数-1` 内。
- 返回值：恒 `0`。
```erb
@SYSTEM_TITLE
HOTKEY_STATE_INIT 4        ; 准备 4 个状态槽
HOTKEY_STATE 0, 0          ; 通常视图
HOTKEY_STATE 1, 0          ; 时间未停止
HOTKEY_STATE 2, 0          ; 备用（例：由 HOTKEY.ERB 的 RETURN STATE:2 取出）
HOTKEY_STATE 3, 0          ; 备用
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:351`（`["HOTKEY_STATE_INIT"] = new HotkeyStateInitMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7609`（`HotkeyStateInitMethod`）→ `UI/Game/HotkeyState.cs:48`（`HotkeyStateInit`）

```text
HotkeyStateInitMethod:
    构造:
        ReturnType = Int64
        argumentTypeArrayEx = [ { ArgTypes = { Int }, OmitStart = 1 } ]
            # 上界仍是 1（非可变参数），故实际必须正好 1 个参数
        CanRestructure = false
    GetIntValue(exm, arguments):
        a0 = arguments[0] 的整数值
        GlobalStatic.Console.Window.hotkeyState.HotkeyStateInit((nint)a0)
        返回 0                              # 恒为 0

HotkeyState.HotkeyStateInit(size)（HotkeyState.cs:48）:
    state = new nint[size]                   # 重新分配 → 旧状态全部丢失
    availableStateArray = true               # 之后 HOTKEY_STATE 才允许写入
    # 注意：源码注释掉了 Toggle()，本函数不会自动开启热键
```

## 备注

- 语义据源码；两套中文文档、EE readme、EM readme 均未收录该函数。`UI/Game/HotkeyState.cs:1-38` 的英文块注释是其唯一说明书。
- `OmitStart = 1` 配合单个 `ArgType.Int` 使参数个数被夹在 1~1，因此**必须**给参数；与 `HOTKEY_STATE` 不同，本函数不存在「允许省略但实现不支持」的问题。
- 参数为负或 0 时，`new nint[size]` 对负数会抛 `OverflowException`，对 0 会得到空数组（此后任何 `HOTKEY_STATE` 写入都越界）；源码不做校验。
- 不调用本函数并不妨碍热键功能被开启：`HotkeyState.Toggle` 在 `availableStateArray == false` 时会提示「hotkeys ON, HOTKEY_STATE_INIT not called.」，但 `keyToNumberRunInterpreter` 随后仍会因 `!availableStateArray` 直接返回 -1（无热键生效）。
