# SKIPLOG

- **类别**：EE 扩展命令
- **签名**：
  - `SKIPLOG <数值表达式>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「概要」节（・SKIPLOG，约 193 行）；`EmueraEE_changelog.txt:72`「SKIPLOG命令追加」。ecd 文档未收录；zh 套件未收录。

## 语义

手动控制消息跳过（messkip）状态。传入非 0 值时，强制进入「正在用右键等手段跳过消息」的状态（此后的输入等待会被自动连续放行）；传入 0 时强制解除跳过状态。相当于在脚本侧代替玩家按下/松开右键跳过。

## 用法

### `SKIPLOG <数值表达式>`
- `<数值表达式>`：非 0 开启跳过状态，0 强制解除。
```erb
;长演示画面自动跳过
SKIPLOG 1
FOR I, 0, 100
    PRINTL 演示文本……
    WAIT
NEXT
SKIPLOG 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:380`（enum）；`Runtime/Script/Statements/FunctionIdentifier.cs:335`（`addFunction(FunctionCode.SKIPLOG, argb[FunctionArgType.INT_EXPRESSION], METHOD_SAFE | EXTENDED)`，`#region EE_SKIPLOG`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:783-790`（`doInstruction` 的 switch 中 `#region EE_SKIPLOG` 分支）

```text
case FunctionCode.SKIPLOG:
    iValue = 参数为常量 ? 常量值 : ((ExpressionArgument)参数).Term.GetIntValue(exm)
    console.MesSkip = (iValue != 0)

# MesSkip 是 EmueraConsole 的公开字段（UI/Game/EmueraConsole.cs:1102）：
#   - PressEnterKey 进入时 MesSkip = keySkip（右键跳过时为 true）
#   - WaitInput 状态下 while (MesSkip ...) 循环会不停以空输入 RunEmueraProgram，
#     实现「跳过中连续放行输入」
#   - 宏循环结束后 MesSkip = false 复位
```

## 备注

- SKIPLOG 置 1 后，下一次进入输入等待即表现为跳过；循环中每次 WAIT/INPUT 都会被自动放行，直到用 `SKIPLOG 0` 解除或玩家操作复位（`PressEnterKey` 会用实际的按键状态覆盖 `MesSkip`）。
- 与 `MOUSESKIP()`/`MESSKIP()` 式中函数（读取 MesSkip 状态，`Runtime/Script/Statements/Function/Creator.cs:41-42`）对应，SKIPLOG 是其写入端。
- ecd 文档未收录；语义以 EE readme 为准。
