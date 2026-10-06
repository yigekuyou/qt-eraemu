# TWAIT

- **类别**：命令
- **签名**：
  - `TWAIT <数值>`, `<数值>`
- **文档来源**：`ecd/docs/translation/Command.md`「输入·等待」→「TWAIT」；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

带时间限制的等待指令。第 1 参数是限制时间（毫秒），第 2 参数是输入接收标志。在限制时间经过之前停止运行，实际行为随输入接收标志而变化：

- 输入接收标志 = 0：接收输入，一旦有输入，即使未到限制时间也会继续下一步。
- 输入接收标志 ≠ 0：不接收输入（可以强制等待到限制时间）。

与同节的 `TINPUT`/`TINPUTS`（限时输入）不同，`TWAIT` 不把任何输入值写入变量，只用于"限时等待或限时打断"。

## 用法

### `TWAIT <数值>`, `<数值>`
- 第 1 个 `<数值>`：限制时间（毫秒）。
- 第 2 个 `<数值>`：输入接收标志。0 表示有输入即提前继续；非 0 表示忽略输入、强制等到限制时间。
```erb
PRINTL 3 秒内按任意键可跳过……
TWAIT 3000, 0
PRINTL 有输入则提前到这里
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:200` → `new TWAIT_Instruction()`；枚举于 `Runtime/Script/Statements/BuiltInFunctionCode.cs:47`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:781-804`（`TWAIT_Instruction`）

```text
构造:
    参数构造器 = SP_SWAP（两个整数表达式 X、Y）
    flag = IS_PRINT | EXTENDED

运行期 DoInstruction():
    exm.Console.ReadAnyKey()                       # 先做一次普通按键等待（见备注）
    arg = (SpSwapCharaArgument)func.Argument
    time = arg.X.GetIntValue(exm)                  # 第 1 参数：限制时间
    flag = arg.Y.GetIntValue(exm)                  # 第 2 参数：输入接收标志
    req = new InputRequest { InputType = InputType.EnterKey }
    若 flag != 0:
        req.InputType = InputType.Void             # 不接收任何输入
    req.Timelimit = time
    exm.Console.WaitInput(req)                     # 带时限的输入等待；到时或满足输入条件后继续
```

## 备注

- **文档与源码差异**：ecd 文档只描述"在限制时间经过之前停止运行"，未提及实现中在设置限时等待之前先调用了一次 `exm.Console.ReadAnyKey()`（无时限的按键等待）。这意味着实际运行会先阻塞到用户敲过一次键（或控制台返回）之后才进入限时等待；文档未覆盖这一行为。
- 源码中 flag ≠ 0 用 `InputType.Void` 实现"不接收输入"，与文档"输入接收标志 ≠ 0：不接收输入"一致。
- 参数为 `SP_SWAP` 构造器，即两个普通整数表达式。
- zh 文档未收录本命令。
