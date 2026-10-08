# QUIT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令
- **签名**：`QUIT`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md` 未收录独立小节（仅在 `General.md` 的 `;!;` 节与 `ERB_Statements.md` 的命令示例中出现）；`Era-Chinese-Documentation/docs/ERB_File_Format.md:1125`（「`QUIT`：退出游戏」）。

## 语义

结束游戏，退出 Emuera。

实现上只是把控制台状态置为 `Quit`：主循环在每条指令执行后检查 `console.IsRunning`，发现不是运行中状态即返回，脚本停止执行，随后窗口关闭、程序结束。QUIT 之后同一函数内的剩余行不会被执行。

常与 `;!;`（只由 Emuera 之外的环境执行）或 `[SKIPSTART]`/`[SKIPEND]` 配合，用于在不兼容环境下直接退出（ecd/General.md）。

## 用法

### `QUIT`
无参数。

```erb
PRINTW 感谢游玩，游戏结束。
QUIT
PRINTL 这一行永远不会被执行
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:218`（`argb[FunctionArgType.VOID]`，无 METHOD_SAFE 等附加标志，注释「ゲームを終了」）
- 实现：`Runtime/Script/Process.ScriptProc.cs:299-301`（switch-case `FunctionCode.QUIT`）；`Quit` 在 `UI/Game/EmueraConsole.cs:475`；主循环退出检查在 `Runtime/Script/Process.ScriptProc.cs:89-91`

```text
执行期：
    控制台.Quit():
        控制台状态 state = ConsoleState.Quit

主循环在每条指令后：
    若 !console.IsRunning（即 state 已非 Running）或 state.ScriptEnd：
        从脚本执行循环 return（脚本终止，进入关闭流程）
```

## 备注

- 相关但不同的 EE 扩展命令（同一 switch 区域，`Runtime/Script/Process.ScriptProc.cs:302-310`）：`QUIT_AND_RESTART`（置 `Program.rebootFlag = true` 后退出并重启）、`FORCE_QUIT`（`Console.ForceQuit()`，强制退出，可选重启确认框）。这些是本仓库 EE 版新增，文档均未收录。
- `zh/Command.md` 未收录；zh/Debug_Command.md 仅提及调试命令 `@EXIT` 与 QUIT 等效。
- 文档对 QUIT 只有「退出游戏」一句，其余语义均来自源码。
