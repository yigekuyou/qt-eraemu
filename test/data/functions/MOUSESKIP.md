# MOUSESKIP

- **类别**：式中函数
- **签名**：
  - `int MOUSESKIP()`
- **文档来源**：`ecd/Command.md`「显示处理·字体处理·显示方式参考」→「MOUSESKIP」小节；`ecd/Expression.md`「内置表达式内函数一览」；zh 套件未收录

## 语义

当前处于"消息跳过"状态（玩家通过右键/按住回车跳过 `WAIT` 等待）时返回 `1`，否则返回 `0`。宏处理时的跳过不置位该状态，返回 `0`；宏跳过与右键跳过同时发生时优先宏处理，也返回 `0`。此函数是 `MESSKIP` 的旧名：当前版本解析到 `MOUSESKIP()` 会输出"建议改用 MESSKIP"的警告，两者行为完全相同。

## 用法

### MOUSESKIP()

- 无参数，`()` 不可省略。

```erb
IF MOUSESKIP()
	; 跳过状态下省略演出
ENDIF
; 现行版本推荐写作 MESSKIP()
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:41`（`["MOUSESKIP"] = new MesSkipMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2544`（`MesSkipMethod`，`warn = true`；状态源 `UI/Game/EmueraConsole.cs:1102`、`:1206`、`:1265`、`:1292`）

```text
function MOUSESKIP(args):
    ; 解析期：args.Count > 0 → 解析错误"参数过多"
    ; warn = true → 解析期警告"函数 MOUSESKIP 已废弃，请改用 MESSKIP"
    return Console.MesSkip ? 1 : 0
```

## 备注

- 文档说"当正在通过右键进入 WAIT 跳过状态时返回 1"；源码返回的是 `Console.MesSkip`，按住回车的键跳过与 `\e` 输入同样会置位该标志，文档描述略窄于实际行为，参见 `MESSKIP.md` 备注。
- 文档补充："宏处理时的跳过返回 0。当宏处理的跳过与右键同时发生时，优先宏处理，返回 0。"宏跳过走 `KillMacro` 路径，不经过 `PressEnterKey` 的 `keySkip` 置位，与源码一致。
- 本名已废弃，仅作兼容保留；新脚本应使用 `MESSKIP()`。
