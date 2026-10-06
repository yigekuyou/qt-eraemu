# MESSKIP

- **类别**：式中函数
- **签名**：
  - `int MESSKIP()`
  - `int MOUSESKIP()`（已废弃的旧名，一并记载，详见 `MOUSESKIP.md`）
- **文档来源**：`ecd/Command.md`「显示处理·字体处理·显示方式参考」→「MOUSESKIP」小节（只记载旧名 `MOUSESKIP`）；`ecd/Expression.md` 只列 `MOUSESKIP`，未列 `MESSKIP`；zh 套件未收录

## 语义

当前处于"消息跳过"状态时返回 `1`，否则返回 `0`。所谓消息跳过状态，即玩家按住回车（或右键）连续跳过 `WAIT` 等待、或输入中包含 `\e` 时的状态；跳过结束（一次等待处理完毕）后自动复位为 `0`。该函数只读状态，无副作用，无参数（写参数会在解析期报"参数过多"）。

## 用法

### MESSKIP()

- 无参数，`()` 不可省略。

```erb
IF MESSKIP()
	; 跳过状态下省略冗长演出
ELSE
	PRINTL 演出文本……
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:42`（`["MESSKIP"] = new MesSkipMethod(false)`；`Runtime/Script/Statements/Function/Creator.cs:42` 为 `MOUSESKIP = new MesSkipMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2544`（`MesSkipMethod`；状态源 `UI/Game/EmueraConsole.cs:1102` `public bool MesSkip;`、`:1206` `PressEnterKey` 中赋值、`:1265` `\e` 输入置位、`:1292` 复位）

```text
function MESSKIP(args):            ; MOUSESKIP 同类，仅构造参数 warn=true
    ; 解析期：args.Count > 0 → 解析错误"参数过多"
    ; warn=true（MOUSESKIP）时另发解析警告"该函数已废弃，请改用 MESSKIP"
    return Console.MesSkip ? 1 : 0

; Console.MesSkip 的生命周期：
;   PressEnterKey(keySkip, ...) 进入时 MesSkip = keySkip（按住回车/右键跳过为 true）
;   输入含 "\e" 时置 MesSkip = true
;   每次等待输入处理完（while MesSkip && state == WaitInput 循环结束后）MesSkip = false
```

## 备注

- 文档（`MOUSESKIP` 小节）称"正在通过右键进入 WAIT 跳过状态时返回 1""宏处理时的跳过返回 0"。源码层面返回的是 `Console.MesSkip`，该标志同样会被按住回车的键跳过以及 `\e` 输入置位；宏跳过走 `KillMacro` 路径、不经 `PressEnterKey` 的 `keySkip`，与文档说法一致。即文档描述偏窄（只提右键），实际为"任何消息跳过状态"。
- `MOUSESKIP` 是旧名，解析期会输出"建议改用 MESSKIP"的警告（`trerror.FuncDeprecated`）；两者返回值完全相同。
