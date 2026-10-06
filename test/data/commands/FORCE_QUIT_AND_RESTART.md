# FORCE_QUIT_AND_RESTART

- **类别**：EE 扩展命令
- **签名**：
  - `FORCE_QUIT_AND_RESTART`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・FORCE_QUIT_AND_RESTART」；`EmueraEE_changelog.txt`（EEv11 追加）。ecd 套件与 zh 套件均未收录本命令。

## 语义

不等待输入、立即结束游戏并重启 Emuera（重新加载 ERB/CSV 后回到启动状态），相当于「不等待输入的 `QUIT_AND_RESTART`」。

若在未夹入任何输入等待的情况下被连续执行，会弹出对话框警告「FORCE_QUIT_AND_RESTART 在没有输入等待的情况下被连续执行，是否不重启直接退出？」：选「是」则放弃重启并以代码错误中止；选「否」则继续重启。防护标志在控制台进入退出／出错／等待按键状态时自动复位（`UI/Game/EmueraConsole.cs` 的 `IsWaitingEnterKey`/`IsWaitAnyKey`）。

## 用法

### `FORCE_QUIT_AND_RESTART`
```erb
;改完 ERB 后让玩家一键重载重启
PRINTL 即将重启以加载新脚本。
FORCE_QUIT_AND_RESTART
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:372`（枚举 `FORCE_QUIT_AND_RESTART`）；`Runtime/Script/Statements/FunctionIdentifier.cs:419`（`addFunction(..., argb[FunctionArgType.VOID])`，无参数）
- 实现：`Runtime/Script/Process.ScriptProc.cs:310`（switch-case，`#region EE_FORCE_QUIT系`）；退出/重启逻辑 `UI/Game/EmueraConsole.cs:477`（`ForceQuit()`）；防护标志 `GlobalStatic.cs:44-46`（`public static bool ForceQuitAndRestart;//連続実行を防ぐ`）；复位点 `UI/Game/EmueraConsole.cs:348,354,366,480-498`；重启执行 `Program.cs:318,380`（`rebootFlag`）

```text
# Process.ScriptProc.cs
case FunctionCode.FORCE_QUIT_AND_RESTART:
    Program.rebootFlag = true          # 与 QUIT_AND_RESTART 相同
    Console.ForceQuit()
    break

# EmueraConsole.ForceQuit()（与 FORCE_QUIT 共用）
ForceQuit():
    if GlobalStatic.ForceQuitAndRestart == true:      # 上一轮 FORCE_QUIT 系刚执行过且未经输入等待
        result = MessageBox.Show(trmb.ForceQuitAndRestart,   # 「…連続実行されました。
                                 "FORCE_QUIT_AND_RESTART",    #   再起動せず終了しますか？」
                                 YesNo)
        if result == Yes:
            Program.rebootFlag = false
            throw CodeEE(trerror.ForceQuitAndRestartError)   # 不重启，报错中止
    if Program.rebootFlag: window.Reboot()    # 退出并重启进程（Program 主循环检测 rebootFlag）
    else:                  Application.Exit()
    GlobalStatic.ForceQuitAndRestart = true   # 记录「刚发生过一次强制退出」

# 防护标志复位：控制台进入 Quit/Error 状态，或 WaitInput 且请求为 AnyKey/EnterKey 时
IsWaitingEnterKey / IsWaitAnyKey:
    GlobalStatic.ForceQuitAndRestart = false
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准，两者一致。
- 与 `QUIT_AND_RESTART`（`Runtime/Script/Process.ScriptProc.cs:303`，置 `rebootFlag` 后走普通 `Quit()`，有输入等待）的差异仅在是否等待输入；两者都通过 `Program.rebootFlag` 触发重启。
- 「連続実行を防ぐ」的计数粒度是全局单标志位：只能识别「上一次强制退出后没有任何输入等待」的情况，无法区分是 `FORCE_QUIT` 还是 `FORCE_QUIT_AND_RESTART` 触发。
