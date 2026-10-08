# QUIT_AND_RESTART

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令
- **签名**：
  - `QUIT_AND_RESTART`
- **文档来源**：EM+EE 在线文档「QUIT_AND_RESTART」；`EmueraEE_readme.txt`「・QUIT_AND_RESTART」条目；`ecd/Command.md` 未收录；zh 套件未收录。

## 语义

重启 Emuera 的命令。与 `QUIT` 一样，会插入相当于一次 WAIT 的输入等待（玩家按回车后窗口才关闭），随后重新启动 Emuera（回到初始加载流程，相当于重启客户端，而非只回到标题画面）。无参数、无返回值。

EE v11 加入（changelog：`関数追加：GDRAWGWITHROTATE, QUIT_AND_RESTART, FORCE_QUIT, FORCE_QUIT_AND_RESTART`）。相关命令：`QUIT`（直接退出）、`FORCE_QUIT`（不等待直接退出）、`FORCE_QUIT_AND_RESTART`（不等待直接重启；连续执行时弹确认对话框防误触）。

## 用法

### `QUIT_AND_RESTART`
- 无参数。
```erb
;例：ERB 更新后请求重启以重新加载
PRINTL 即将重启 Emuera，按回车继续。
QUIT_AND_RESTART
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:417`（`addFunction(FunctionCode.QUIT_AND_RESTART, argb[FunctionArgType.VOID])`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:370`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:303`（`#region EE_FORCE_QUIT系`，`case FunctionCode.QUIT_AND_RESTART`）；重启执行 `UI/Game/EmueraConsole.cs:490-495`、`:1211`；全局标志 `GlobalStatic.cs:44`、`Program.cs:380`

```text
# 命令执行（Process.ScriptProc.cs）:
case FunctionCode.QUIT_AND_RESTART:
    Program.rebootFlag = true        # 置「要重启」标志
    exm.Console.Quit()               # state = ConsoleState.Quit（与 QUIT 相同）

# 退出流程（EmueraConsole）:
Quit() 只把状态设为 Quit；脚本流结束后停在 Quit 状态等待一次输入
（≈ WAIT 一次的效果）。
玩家按回车（PressEnterKey，state == Quit）时:
    若 Program.rebootFlag: window.Reboot()    # 重新初始化并重启 Emuera
    否则:                  window.Close()     # 普通 QUIT 直接关窗
```

## 备注

- 文档「和 QUIT 一样，插入 WAIT 后重启 Emuera」与实现一致：重启延迟是通过「Quit 状态 + 一次回车」实现的，而非立即重启。
- 注意与本仓库中已注释掉的老实现（`Program.cs` 内 `Application.Restart()` 注释块）不同，现行实现由 `window.Reboot()` 完成进程内重启。
- FORCE_QUIT_AND_RESTART（本批之外的兄弟命令）才有防连续执行的确认对话框；QUIT_AND_RESTART 本身不弹对话框。
