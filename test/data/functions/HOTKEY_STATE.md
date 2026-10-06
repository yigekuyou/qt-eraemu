# HOTKEY_STATE

- **类别**：式中函数（EE 扩展 / 热键系统）
- **签名**：`int HOTKEY_STATE(int 索引, int 值)`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）与 EE readme 均未收录本函数；语义据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:7589` 的 `HotkeyStateMethod`）与 `UI/Game/HotkeyState.cs:1-38` 的说明注释、`:55` 的 `HotkeyStateSet`。

## 语义

设置热键状态数组的第 `索引` 个元素为 `值`，等价于 C# 侧的 `state[index] = value`。它本身不触发任何输入，只写入一份供 `HOTKEY.ERB` 判定用的状态；`HOTKEY.ERB` 中用 `STATE:n == v` 读取该状态，用 `STATE:n` 取状态值参与返回编号的计算。

使用前提（源码注释 `UI/Game/HotkeyState.cs:17-38`）：

- 必须先调用 `HOTKEY_STATE_INIT` 建立状态数组，否则调用本函数会抛出运行时异常（C# 侧直接 `throw new Exception("use HOTKEY_STATE_INIT before using HOTKEY_STATE")`，不是 `CodeEE`）。
- 热键系统整体还需要：把 `HOTKEY.ERB.example.txt` 改名为 `HOTKEY.ERB` 放在 exe 同目录（**不能**放在 ERB 文件夹里），并在运行中按 Ctrl+D 开启热键。
- 返回值恒为 `0`。

**源码瑕疵**：注册的参数表允许省略第 2 个参数（`OmitStart = 1`，写成 `HOTKEY_STATE n` 可通过解析期检查），但实现无条件读取 `arguments[1]`，省略时会以 `ArgumentOutOfRangeException`/空引用崩溃。实用中必须始终写满两个参数（此判断据 `ArgumentBuilder.popTerms` 不补空参数、`FunctionMethod.CheckArgumentTypeEx` 允许 `null` 占位推得，标注为推定）。

## 用法

### int HOTKEY_STATE(索引, 值)
- 索引：状态数组下标，从 0 开始。数组长度由 `HOTKEY_STATE_INIT` 决定，越界会以 C# 数组越界异常崩溃（源码无边界检查）。
- 值：整数，写入的状态值。
- 返回值：恒 `0`。
```erb
@SYSTEM_TITLE
HOTKEY_STATE_INIT 3        ; 状态数组长度 3（必须先执行）
HOTKEY_STATE 0, 1          ; state[0] = 1  → 表示「看着角色时的通常视图」
HOTKEY_STATE 1, 0          ; state[1] = 0  → 表示「时间未停止」
HOTKEY_STATE 2, 811        ; state[2] = 811→ 由 HOTKEY.ERB 通过 STATE:2 取出当作返回编号
```

配套的 `HOTKEY.ERB`（放在 exe 同目录，非 ERB 文件夹；只支持 `IF`/`ENDIF`/`SIF`/`RETURN`，不支持 `ELSEIF`/`ELSE`）：

```erb
@HOTKEY(KEY)
IF STATE:0 == 1
    IF KEY == KEYS:D
        RETURN 811
    ENDIF
    IF KEY == KEYS:F
        RETURN 400
    ENDIF
ENDIF
IF KEY == KEYS:T
    RETURN STATE:2
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:350`（`["HOTKEY_STATE"] = new HotkeyStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7589`（`HotkeyStateMethod`）→ `UI/Game/HotkeyState.cs:55`（`HotkeyStateSet`）

```text
HotkeyStateMethod:
    构造:
        ReturnType = Int64
        argumentTypeArrayEx = [ { ArgTypes = { Int, Int }, OmitStart = 1 } ]
            # 解析期允许 1~2 个参数：第 2 个可省略（但实现并不处理省略，见备注）
        CanRestructure = false
    GetIntValue(exm, arguments):
        a0 = arguments[0] 的整数值
        a1 = arguments[1] 的整数值          # 省略第 2 参数时在此处崩溃
        GlobalStatic.Console.Window.hotkeyState.HotkeyStateSet((nint)a0, (nint)a1)
        返回 0                              # 恒为 0

HotkeyState.HotkeyStateSet(index, value)（HotkeyState.cs:55）:
    若 !availableStateArray:                 # 尚未 HOTKEY_STATE_INIT
        throw new Exception("use HOTKEY_STATE_INIT before using HOTKEY_STATE")
    state[index] = value                     # 无下标检查
```

## 备注

- 语义据源码；两套中文文档、EE readme、EM readme 均未收录 `HOTKEY_STATE`/`HOTKEY_STATE_INIT`。`UI/Game/HotkeyState.cs:1-38` 的英文块注释是本功能唯一的「说明书」，本文的用法部分即据其翻译。
- 状态数组是 `nint[]`（`UI/Game/HotkeyState.cs:44`），`HOTKEY.ERB` 求值 `STATE:n` 时按 `(int)state[n]` 转回整数（`UI/Game/HotkeyState.cs` 的 `InterpreterEval`）。
- `HotkeyStateSet` 抛的是普通 `Exception` 而非 `CodeEE`，因此「忘了先 INIT」这类错误不会以 Emuera 的脚本错误形式显示，而是按未捕获异常处理。
- 热键功能整体只在 Windows/Forms 前端有意义；`HOTKEY.ERB` 的解析器只支持 `KEY == KEYS:xxx`、`STATE:n == v`、`STATE:n`、数字与 `IF`/`SIF`/`ENDIF`/`RETURN`（`HotkeyState.ParseEval`/`Parse`）。
- `HOTKEY_STATE` 与同名系列没有对应的读取函数；读取只能通过 `HOTKEY.ERB` 解释器或 `STATE:` 语法。
