# BEGIN

- **类别**：命令
- **签名**：
  - `BEGIN <关键字>`
- **文档来源**：`ecd/docs/translation/Command.md`「调试辅助·系统流的控制」→「BEGIN `<关键字>`」；`Era-Chinese-Documentation/docs/ERB_File_Format.md`（BEGIN 系统指令说明）及 `docs/Flow.md`（各状态流转）。

## 语义

切换游戏流程到 `<关键字>` 指定的系统状态，相当于 Eramaker 时代就有的指令（Emuera 新增 `FIRST` 和 `TITLE`）。执行后当前函数立即终止、不再返回——即使 `BEGIN` 是被 `CALL` 调用的函数中执行的，也不会回到原函数；其效果相当于执行了 `RETURN 0`。合法关键字共 7 个：`SHOP`、`TRAIN`、`AFTERTRAIN`、`ABLUP`、`TURNEND`、`FIRST`、`TITLE`（关键字大小写不敏感，解析时已 Trim+ToUpper）。

`BEGIN FIRST` 与标题画面选择「从头开始」等效，会调用事件函数 `@EVENTFIRST`；`BEGIN TITLE` 返回标题画面。两者都不做变量初始化，需要时请自行执行 `RESETDATA`。若当前系统状态不允许 BEGIN（非 `TITLE`），或关键字不合法，抛 CodeEE。

## 用法

### `BEGIN <关键字>`
- `<关键字>`：以下之一——`TITLE`（回标题）、`FIRST`（新游戏，触发 `@EVENTFIRST`）、`SHOP`、`TRAIN`（触发 `@EVENTTRAIN`）、`AFTERTRAIN`（触发 `@EVENTEND`）、`ABLUP`、`TURNEND`（触发 `@EVENTTURNEND`）。
```erb
@EVENTEND
PRINTL 游戏结束，回到标题。
BEGIN TITLE
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:224`（`new BEGIN_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3037`（`BEGIN_Instruction`）；关键字解析在 `Runtime/Script/Process.State.cs:181`（`ProcessState.SetBegin`）

```text
指令类 BEGIN_Instruction:
    flag = FLOW_CONTROL
    参数为 STR（编译期常量字符串）

    DoInstruction(exm, func, state):
        keyword = func.Argument.ConstStr
        state.SetBegin(keyword, force=true)   # EE 分支：旧签名无 force
        state.Return(0)                       # 相当于 RETURN 0
        exm.Console.ResetStyle()              # 清除 PRINTSTYLE 等显示状态

SetBegin(keyword, force):
    根据 keyword 分派（SHOP/TRAIN/AFTERTRAIN/ABLUP/TURNEND/FIRST/TITLE）;
    SHOP 与 FIRST 还会卸载临时加载的图像资源。
    关键字不在列表中 → 抛 CodeEE（无效的 BEGIN 参数）。
    SetBegin(BeginType, force):
        force=true 时跳过状态检查；BEGIN 传 force=true，
        即任何状态下都允许设置 begintype（TITLE 更是任何情况都允许）。
    begintype = type   # 脚本结束（ScriptEnd）时由 Process 据此进入下一个系统状态
```

## 备注

- 同文件中另有 EE 扩展 `FORCE_BEGIN`（`FORCE_BEGIN_Instruction`，`Runtime/Script/Statements/Instraction.Child.cs:3056`），同样调用 `SetBegin(keyword, true)`；EE 修订后普通 `BEGIN` 也传入 `force=true`（源码注释 `state.SetBegin(keyword, true)` 取代了旧的 `SetBegin(keyword)`），因此源码中「状态不允许 BEGIN」的检查路径对标准 BEGIN 实际不生效——这与早期文档描述的「部分状态下不能 BEGIN」存在实现层面的差异。
- ecd 文档只列出新旧关键字的差异（`FIRST`/`TITLE` 为新增）；`zh/Flow.md` 详细描述了各 BEGIN 状态的流转（如 `@SYSTEM_TITLE` 中不执行 `BEGIN`/`LOADDATA` 而直接 RETURN 会报错终止），可与本命令互为补充。
- 实际的状态切换发生在脚本结束之后（`Runtime/Script/Process.SystemProc.cs` 的 `beginTitle`/`beginFirst`/`beginTrain` 等），`BEGIN` 本身只记录目标状态并终止当前脚本。
