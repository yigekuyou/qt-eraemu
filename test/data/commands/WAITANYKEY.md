# WAITANYKEY

- **类别**：命令
- **签名**：`WAITANYKEY`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md` → `### WAITANYKEY`；`Era-Chinese-Documentation/docs/`（zh/Command.md）未收录。

## 语义

等待任意按键输入或鼠标点击的 `WAIT` 指令，可以说是 `WAIT` 的 `ONEINPUT` 版。与 `WAIT`（仅 Enter/确认）不同，任意键或鼠标点击都会解除等待。和 `WAIT` 一样，在系统文本跳过（skipPrint）状态下会被整体跳过。

## 用法

### WAITANYKEY
无参数，无返回值。

```erb
PRINTL 按任意键继续……
WAITANYKEY
PRINTL 已继续。
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:201`（`new WAITANYKEY_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:768`（`WAITANYKEY_Instruction`）→ `UI/Game/EmueraConsole.cs:661`（`EmueraConsole.ReadAnyKey`）

```text
类 WAITANYKEY_Instruction:
    ArgBuilder <- VOID（无参数）
    flag <- IS_PRINT                    // skipPrint 期间整条被跳过
    函数 DoInstruction(exm, func, state):
        exm.Console.ReadAnyKey(anykey: true, stopMesskip: false)

// ReadAnyKey（EmueraConsole）中 anykey=true 分支：
//     req.InputType <- AnyKey        // 任意键或鼠标点击都接受
//     req.StopMesskip <- false
//     inputReq <- req; state <- ConsoleState.WaitInput
//     process.NeedWaitToEventComEnd <- false
```

对比：`WAIT` 调用 `ReadAnyKey(false, false)`（InputType = EnterKey），`FORCEWAIT` 调用 `ReadAnyKey(false, true)`，`WAITANYKEY` 调用 `ReadAnyKey(true, false)`——三者的差别只在 InputType 与 StopMesskip 两个参数上。

## 备注

- ecd 文档只说它是"WAIT 的 ONEINPUT 版"，未描述跳过行为；源码可见它与 WAIT 一样带 `IS_PRINT` 标志，系统文本跳过期间会被跳过（此时应改用 FORCEWAIT 系）。
- 与 `ONEINPUT`/`TONEINPUT` 不同，WAITANYKEY 不读取输入内容到 `RESULT`/`RESULTS`，只是等待。
