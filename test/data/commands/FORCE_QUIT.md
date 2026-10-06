# FORCE_QUIT

- **类别**：EE 扩展命令
- **签名**：
  - `FORCE_QUIT`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・FORCE_QUIT」；`EmueraEE_changelog.txt`（EEv11 与 `QUIT_AND_RESTART`、`FORCE_QUIT_AND_RESTART`、`GDRAWGWITHROTATE` 一同追加）。ecd 套件与 zh 套件均未收录本命令。

## 语义

不等待输入、立即结束游戏并退出程序。与普通 `QUIT` 的差别：`QUIT` 结束脚本执行后会经过一次「按任意键」的输入等待再关闭窗口，`FORCE_QUIT` 跳过该等待直接退出。

与 `FORCE_QUIT_AND_RESTART` 的差别：本命令不重启，直接结束进程。若此前刚用 `FORCE_QUIT_AND_RESTART` 连续强制退出过，会先弹出对话框询问「是否不重启直接退出」（见下）。

## 用法

### `FORCE_QUIT`
```erb
PRINTL 感谢游玩。
FORCE_QUIT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:371`（枚举 `FORCE_QUIT`）；`Runtime/Script/Statements/FunctionIdentifier.cs:418`（`addFunction(FunctionCode.FORCE_QUIT, argb[FunctionArgType.VOID])`，无参数）
- 实现：`Runtime/Script/Process.ScriptProc.cs:307`（switch-case，`#region EE_FORCE_QUIT系`）；退出逻辑 `UI/Game/EmueraConsole.cs:477`（`ForceQuit()`）；连续执行防护标志 `GlobalStatic.cs:44-46`

```text
# Process.ScriptProc.cs
case FunctionCode.FORCE_QUIT:          # 无 rebootFlag 设置
    Console.ForceQuit()
    break

# EmueraConsole.ForceQuit()
ForceQuit():
    if GlobalStatic.ForceQuitAndRestart == true:      # 之前刚执行过 FORCE_QUIT_AND_RESTART
        result = MessageBox.Show(「FORCE_QUIT_AND_RESTARTが入力待ちを挟まず
                                  連続実行されました。再起動せず終了しますか？」,
                                 "FORCE_QUIT_AND_RESTART", YesNo)
        if result == Yes:
            Program.rebootFlag = false
            throw CodeEE(「FORCE_QUIT_AND_RESTARTが連続実行されました」)   # 中止退出
    if Program.rebootFlag:
        window.Reboot()                               # （FORCE_QUIT 时 rebootFlag 恒为 false）
    else:
        Application.Exit()                            # 直接退出整个程序，无输入等待
    GlobalStatic.ForceQuitAndRestart = true
    return
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准。
- readme 说的是「入力待ちせずにQUITする命令」，与源码一致：`QUIT` 的实现（`Runtime/Script/Process.ScriptProc.cs:303`）只置 `ConsoleState.Quit`（之后仍有等待），`FORCE_QUIT` 则走 `Application.Exit()`。
- 连续执行警告对话框逻辑为 `FORCE_QUIT` 与 `FORCE_QUIT_AND_RESTART` 共用（`ForceQuit()` 内），对应 readme 中「入力待ちを挟まず連続実行されるとダイアログボックスで警告が出る」一句。
