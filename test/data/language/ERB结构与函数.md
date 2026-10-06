# ERB 结构与函数

> 来源：ecd/docs/ERB_Structure（ERB 文件结构）、ecd/docs/EraBasic_Structure（EraBasic 的结构）、ecd/docs/Flow（流程图）；zh 套件 ERB_File_Format、EraBasic_Structure、Flow 交叉核对；源码 `Runtime/Script/Loader/ErbLoader.cs`、`Runtime/Script/Parser/LogicalLineParser.cs`、`Runtime/Script/Data/LabelDictionary.cs`、`Runtime/Script/Data/IdentifierDictionary.cs`、`Runtime/Script/Process.cs`、`Runtime/Script/Process.ScriptProc.cs`、`Runtime/Script/Process.State.cs`、`Runtime/Script/Process.CalledFunction.cs`。

## 概述

一个 ERB 文件就是若干「函数」的序列：每个函数以 `@` 行开头，到下一个 `@` 行或文件末尾为止；函数内部可以有 `$` 标签、`#` 预处理指令和一行一句的普通语句。函数分为两类：**事件函数**（如 `@EVENTFIRST`）与**系统函数**（如 `@SHOW_SHOP`）由引擎在流程的固定时机自动调用，可以多重定义；**普通函数**由脚本用 `CALL`/`JUMP` 调用，同一名字只能定义一次。整个游戏的推进由 `BEGIN` 命令驱动的状态机完成（详见 language/内置流程.md），本篇侧重文件结构、函数的定义与调用模型。

## ERB 文件结构

### 文件与编码

- 脚本文件扩展名 `.erb`，头文件 `.erh`，置于 `ERB/` 文件夹（可含子目录）；
- 编码通常为 Shift-JIS（Emuera 会自动探测，也支持 UTF-8）；
- 一行一句：除特殊行外每行一条语句；行首缩进不影响运行。

### 行的种类

| 行首 | 种类 | 说明 |
| --- | --- | --- |
| `@` | 函数定义 | 定义一个函数，到下一个 `@` 或文件末尾结束 |
| `#` | 预处理指令 | 函数属性（`#FUNCTION` 等）与定义（`#DIM` 等），必须紧跟 `@` 行之下 |
| `$` | 标签 | `GOTO` 的跳转目标，属于其上方最近的函数 |
| `[XXX]` | 预处理区块行 | `[SKIPSTART]`、`[IF XXX]` 等，装载期裁剪代码 |
| `;` | 注释 | 整行忽略（`;!;`、`;#;` 例外，见下） |
| 其他 | 普通语句 | 赋值、命令调用等 |

`ErbLoader.loadErb` 按上述顺序逐行分派（`Runtime/Script/Loader/ErbLoader.cs:340-500`）：

```text
// ErbLoader.cs:359-495（伪代码）
while ((st = eReader.ReadEnabledLine(ppstate.Disabled)) != null):
    if st.Current == '[' 且 Next != '[':            // :364
        ppstate.AddKeyWord(token, token2, position) // 预处理区块行，:372
        continue
    if ppstate.Disabled: continue                   // 被裁剪的行，:380
    if st.Current == '#':
        if lastLine 不是 FunctionLabelLine:
            警告 InvalidSharp                       // #:384-389
        LogicalLineParser.ParseSharpLine(...)       // :391，详见 language/预处理与定义.md
    else if st.Current == '$' 或 '@':
        nextLine = LogicalLineParser.ParseLabelLine(st, ...)  // :398
        '@' → labelDic.AddLabel(label)（普通函数重名则警告，:412-422）
        '$' → gotoLabel.ParentLabelLine = lastLabelLine（标签归属函数），
              同函数内重名标签警告，:433-441
    else:
        if lastLabelLine == null: 警告 LineBeforeFunc  // 函数外出现语句，:461-462
        nextLine = LogicalLineParser.ParseLine(...)    // :463
    nextLine.ParentLabelLine = lastLabelLine           // :492
    lastLine = addLine(nextLine, lastLine)             // :494，串成单链表
```

注释的三种特殊写法：`;` 普通注释；`;!;` 只在 Emuera 中生效（Eramaker 视为注释）；`;#;` 只在调试模式下执行。

### 文件的读取顺序

Emuera 读取 `ERB/` 下全部文件，默认顺序取决于文件系统；同名的普通函数以「先读到的」为准可能产生歧义。EE 扩展中名字含 `#` 的子目录先装载（`Runtime/Script/Loader/ErbLoader.cs:51-82`），配置项「読み込み順をファイル名順にソートする」可强制按文件名排序。

## 函数定义

```erb
;普通函数：可带参数
@函数名, ARG:0, ARGS:0
    ; 函数体
    RETURN 1

;事件函数：不能带参数
@EVENTCOMEND
    PRINTW 文本
    RETURN 1
```

- 函数从 `@` 行开始，到下一个 `@` 行或文件末尾结束；一个文件可定义多个函数；
- 参数写在 `@函数名` 之后，数值放 `ARG`、字符串放 `ARGS`，可带默认值（如 `ARG:0 = -1`）；事件函数带参数会报警告（`ErbLoader.parseLabel`，`Runtime/Script/Loader/ErbLoader.cs:551-560`，`EventFuncHasArg`）；
- 函数定义与细节（`#FUNCTION`、#DIM 等指令）详见 language/预处理与定义.md。

### 函数名的两类识别

装载时 `LogicalLineParser.ParseLabelLine` 按函数名给函数打标记（`Runtime/Script/Parser/LogicalLineParser.cs:349-360`）：

```text
// LogicalLineParser.cs:349-360（伪代码）
funclabelLine = new FunctionLabelLine(position, labelName, wc)
if IdentifierDictionary.IsEventLabelName(labelName):
    funclabelLine.IsEvent = true    // 事件函数
    funclabelLine.IsSystem = true
    funclabelLine.Depth = 0
else if IdentifierDictionary.IsSystemLabelName(labelName):
    funclabelLine.IsSystem = true   // 系统函数
    funclabelLine.Depth = 0
```

`IdentifierDictionary.IsEventLabelName`（`Runtime/Script/Data/IdentifierDictionary.cs:54-70`）的事件函数名单是固定的 9 个：`EVENTFIRST`、`EVENTTRAIN`、`EVENTSHOP`、`EVENTBUY`、`EVENTCOM`、`EVENTCOMEND`、`EVENTTURNEND`、`EVENTEND`、`EVENTLOAD`。`IsSystemLabelName`（`Runtime/Script/Data/IdentifierDictionary.cs:71-110`）在此基础上加入 `SHOW_STATUS`、`SHOW_USERCOM`、`USERCOM`、`SOURCE_CHECK`、`SHOW_SHOP`、`USERSHOP`、`SAVEINFO`、`SHOW_JUEL`、`SHOW_ABLUP_SELECT`、`USERABLUP`、`CALLTRAINEND`、`TITLE_LOADGAME`、`SYSTEM_TITLE`、`SYSTEM_AUTOSAVE`、`SYSTEM_LOADEND`，以及正则匹配的 `COMxxx`、`COM_ABLExxx`、`ABLUPxxx` 系列。系统函数由引擎按名字调用，未定义时多数只是跳过。

## 事件函数与普通函数

### 普通函数

- 同名只能定义一个，重复定义会警告 `FuncIsAlreadyDefined`（`Runtime/Script/Loader/ErbLoader.cs:413-422`）；
- 只能用 `CALL`/`JUMP`/`CALLFORM`/`JUMPFORM` 进入，不能当作事件被引擎调用；反之，用 `CALL` 调用事件函数名会抛 `CallToEventFunc`，用事件调用形式调普通函数抛 `CalleventToNonEventFunc`（`Runtime/Script/Process.CalledFunction.cs:83-133`）；
- 函数体执行到末尾（下一个 `@` 或文件尾）相当于隐式 `RETURN 0`：`runScriptProc` 遇到 `FunctionLabelLine`/`NullLine` 时把 `RESULT` 置 0 并执行 `state.Return(0)`（`Runtime/Script/Process.ScriptProc.cs:63-74`）。

### 事件函数

事件函数是游戏生命周期固定位置被调用的钩子，**可以重名**：全部同名事件函数都会被调用。装载完成后 `LabelDictionary.SortLabels` 把同名事件函数分进 4 个组（`Runtime/Script/Data/LabelDictionary.cs:82-111`）：

```text
// LabelDictionary.cs:82-111（伪代码）
eventLabels[0] = onlylist    // #ONLY
eventLabels[1] = prilist     // #PRI
eventLabels[2] = normallist  // 无属性（#PRI 且 #LATER 者二重登记，eramaker 仕様）
eventLabels[3] = laterlist   // #LATER
```

调用时按 0→3 组顺序执行：`#ONLY` 函数 `RETURN 1` 后直接终止整个事件组；`#SINGLE` 函数 `RETURN 1` 后跳到下一组（跳过本组其余同名函数）。这一逻辑在 `ProcessState.Return`（`Runtime/Script/Process.State.cs:400-406`）：

```text
// Process.State.cs:400-406（伪代码）
if called.IsOnly:      called.FinishEvent()      // #ONLY：全部结束
elif called.HasSingleFlag and ret == 1:
                       called.ShiftNextGroup()   // #SINGLE 返回 1：跳到下一组
else:                  called.ShiftNext()        // 继续下一个同名函数
```

`CalledFunction.ShiftNext` 在 4 个组之间推进，`group >= 4` 时 `CurrentLabel = null` 表示全部执行完毕（`Runtime/Script/Process.CalledFunction.cs:286-304`）。典型用法：`#PRI` 用于角色死亡检查，`#LATER` 用于「一天过去了」之类的结算，`#SINGLE` 用于同角色互斥台词。

事件函数本体不能被 `CALL`，也不能带参数；`IntoFunction` 还禁止嵌套调用事件函数——事件链未走完时再进入事件函数抛 `CalleventBeforeFinishEvent`（`Runtime/Script/Process.State.cs:449-456`）。

## 系统流程与 BEGIN（概览）

流程状态机由 `SystemStateCode` 枚举描述（`Runtime/Script/Process.State.cs:17-78`），关键状态带两个标志位：`__CAN_BEGIN__`（此处允许 `BEGIN`）与 `__CAN_SAVE__`（此处允许呼出存读档界面）。主循环在 `Process.DoScript`（`Runtime/Script/Process.cs:330-359`）：

```text
// Process.cs:330-359（伪代码）
DoScript():
    while true:
        while state.ScriptEnd and console.IsRunning:
            runSystemProc()      // 系统流程：状态机驱动，Process.SystemProc.cs
        if !console.IsRunning: break
        runScriptProc()          // 脚本执行：逐行跑函数体，Process.ScriptProc.cs:19-90
```

`BEGIN` 命令的语义是「记住意图 + 立刻终结当前函数链」：`SetBegin(keyword)` 校验关键字并检查当前状态是否带 `__CAN_BEGIN__`（`Runtime/Script/Process.State.cs:181-239`），`BEGIN TITLE` 任何状态都可用；真正切换发生在函数链退空时的 `ProcessState.Begin()`（`Runtime/Script/Process.State.cs:270-318`），它清空 `functionList`（丢弃全部返回地址，即「BEGIN 不会返回」）并把 `SystemState` 置为对应 `*_Begin` 状态。各流程的详细处理顺序见 language/内置流程.md。

## CALL / JUMP / GOTO 调用模型

三种转移的层次不同：

- **GOTO / IF / 循环**：同一函数内的转移，由装载期把 `JumpTo` 直接指向目标行，运行时 `state.JumpTo(line)` 只改 `currentLine`（`Runtime/Script/Process.State.cs:172-178`）；
- **CALL / JUMP**：跨函数转移，走 `IntoFunction`/`Return` 的调用栈。

装载期第三轮 `setJumpTo` 为 CALL/JUMP 系指令解析目标函数是否存在（`Runtime/Script/Loader/ErbLoader.cs:1482-1527`），找不到的函数在装载期即报 `NotDefinedFunc` 警告并标记为错误行。

运行时的调用栈是 `ProcessState.functionList`（`CalledFunction` 列表）：

```text
// 调用模型（伪代码，源码：Process.State.cs:446-513, 363-444；Process.CalledFunction.cs:109-133）
CALL F(args):
    callto = CalledFunction.CallFunction(this, F, retAddress=当前行)  // :109
      // 非事件函数查 noneventLabelDic；IsMethod（#FUNCTION）抛错；找不到返回 null → 运行时报错
    args → ConvertArg（类型检查、默认值补全，Process.CalledFunction.cs:154-231）
    state.IntoFunction(callto, args, exm)      // :446
      // 事件互斥检查 → 引参数实化 SetTransporter → 私有变量 ScopeIn → functionList.Add
      // currentLine = callto.CurrentLabel（@ 行）
    // runScriptProc 继续循环执行 F 的函数体
RETURN ret:
    state.Return(ret)                          // :363
      // 非 JUMP：currentLine = ReturnAddress（CALL 的下一行），functionList 移除栈顶
      // JUMP 进入的函数（IsJump）：直接继续向上 Return，即 JUMP 后的 RETURN 层层弹回
      // functionList 退空且 begintype != NULL → state.Begin()，切换流程
```

要点：

1. **CALL 会返回**：返回地址是调用处的下一行；`RETURN` 的参数写入 `RESULT`（字符串函数写 `RESULTS`）。
2. **JUMP 不返回**：`CalledFunction.IsJump` 置位后，`Return` 对该层直接删除并继续向上传递返回动作（`Runtime/Script/Process.State.cs:377-385`），因此 JUMP 后的 `RETURN` 等价于把被跳函数也一起结束。
3. **GOTO 限于函数内**：`$` 标签在装载期归属其上最近的函数（`Runtime/Script/Loader/ErbLoader.cs:433-441`），跨函数 `GOTO` 在装载期查不到标签即报错。
4. 函数链退空时若没有未决的 BEGIN（`begintype == NULL`）且状态为 `Normal`，下一次 `runSystemProc` 进入 `endNormal`，抛 `UnexpectedScriptEnd`（`Runtime/Script/Process.SystemProc.cs:1105-1108`）——这就是「EVENTFIRST 里不 BEGIN 会报错退出」的来源。

## 与源码的差异/备注

1. ecd/ERB_Structure 说「`GOTO` 只能跳到标签（`$`）或函数（`@`）」——严格说 `GOTO` 只能跳 `$` 标签且限同一函数内；跳函数要用 `JUMP`/`CALL`。文档表述把两者混在一起。
2. ecd/EraBasic_Structure（翻译自 Eramaker 时代文档）称「事件函数可以重名……全部会被调用」，这与 Emuera 一致，但未提到 `#ONLY`（Emuera 独有）以及 `#PRI`+`#LATER` 同时存在时该函数会被**执行两次**（`Runtime/Script/Data/LabelDictionary.cs:100` 注释「二重に登録する。eramakerの仕様」）——文档未提及这一边角行为。
3. ecd/ERB_Structure 的行连接示例 `{ #DIM HOGE = 1,2,3,4 }` 只是示意；实际行连接由 `EraStreamReader` 在装载期拼接物理行（详见 language/预处理与定义.md），与 `#DIM` 语法无关。
4. ecd/ERB_Structure 说「函数从 `@` 行开始」，源码还要求函数外的语句行会警告 `LineBeforeFunc`（`Runtime/Script/Loader/ErbLoader.cs:461-462`）——即所有可执行行必须位于某个函数内部，文档未明说。
5. zh 套件的 ERB_File_Format（Eramaker 篇）称「不支持行尾注释」「REPEAT 不支持嵌套」——这是 Eramaker 的限制；Emuera 支持行中注释（词法器处理 `;`），REPEAT 嵌套仅给出警告 `NestedRepeat`（`Runtime/Script/Loader/ErbLoader.cs:1019-1025`）而非禁止。zh/EraBasic_Structure 与 ecd/EraBasic_Structure 内容一致（同源翻译）。
6. 文档（ecd/Flow）称同名 `@SYSTEM_TITLE` 多重定义时「调用第一次读取到的定义」；源码中非事件函数重名时 `SortLabels` 只保留 `labelList[0]`（读取顺序中的第一个），且默认配置会先发 `FuncIsAlreadyDefined` 警告（`Runtime/Script/Loader/ErbLoader.cs:413-422`），与文档一致但文档未提警告。
