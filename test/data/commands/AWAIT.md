# AWAIT

- **类别**：命令
- **签名**：
  - `AWAIT {<时间>}`
- **文档来源**：`ecd/docs/translation/Command.md`「AWAIT 相关」小节；`Era-Chinese-Documentation` 套件未收录本命令专节（grep 无命中）。

## 语义

暂停 ERB 的执行，并进行 Windows 的消息处理。指定参数时，会等待指定的毫秒数后再继续执行；不指定参数时使用默认行为（等待约 0/默认时长，仅让出处理权）。

`AWAIT` 指令会中断 Emuera 的无限循环警告，防止 Emuera 进程变为「无响应」。在进行耗时处理时请使用它。不过 `AWAIT` 指令本身也需要相当的执行时间，过于频繁反而会变慢；为了不让用户感到不安，建议在循环中逐次显示处理进度。

参数为 0 以上的整数；受计时器特性的影响，极短的时间可能无法按预期工作。

## 用法

### `AWAIT {<时间>}`
- `<时间>`（可省略）：数值表达式，等待的毫秒数。0 时使用默认行为。为负数或超过 10000（10 秒）时报错。
```erb
FOR COUNT, 0, 100
  ;耗时处理
  PRINTL 处理中...
  CLEARLINE 1
  AWAIT
NEXT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:397`（`new AWAIT_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2662`（`AWAIT_Instruction`）

```text
指令类 AWAIT_Instruction:
    参数构造器 = EXPRESSION_NULLABLE（参数可省略）
    flag = EXTENDED   # 源码注释：スキップ不可（原 IS_PRINT | IS_INPUT 被注释掉）

DoInstruction(exm, func, state):
    waittime = -1                          # 默认：未指定参数
    arg = func.Argument as ExpressionArgument
    若 arg != null 且 arg.Term != null:
        waittime = arg.Term 求值（毫秒）
        若 waittime < 0:
            抛出 CodeEE（AwaitArgIsNegative，"AWAIT 的参数不能为负：<waittime>"）
        若 waittime > 10000:
            抛出 CodeEE（AwaitArgIsOver10Seconds，"AWAIT 的参数不能超过 10 秒"）
    exm.Console.Await((int)waittime)       # -1 表示使用控制台默认等待行为
```

## 备注

- ecd 文档写「参数为 0 以上的整数，为 0 时使用默认行为」，源码对省略参数用 `waittime = -1` 传给 `Console.Await`，由控制台层把 -1 解释为默认等待；两者在不同层面描述同一行为，不矛盾。
- 上限 10000 毫秒（10 秒）是源码硬编码的检查，ecd 文档未提及，属文档未记载的实现细节。
- flag 注释显示原版曾带 `IS_PRINT | IS_INPUT`（不可跳过、视为输入），本仓库已改为仅 `EXTENDED`，与旧版行为可能有差异。
- zh 套件未收录本命令。
