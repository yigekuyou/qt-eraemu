# REPEAT

- **类别**：命令
- **签名**：`REPEAT <数值表达式（重复次数）>`
- **文档来源**：`ecd/docs/translation/Command.md` 未收录独立小节（说明散见于「循环·分支语法」FOR 节：「`FOR`～`NEXT` 是 `REPEAT`～`REND` 的增强版……`REPEAT` 固定计数变量 `COUNT:0`、初始值 0、步长 1」）；`Era-Chinese-Documentation/docs/ERB_File_Format.md`「重复与GOTO」节与 `ecd/ERB_Compound_Statements.md`「计数循环：REPEAT」节。

## 语义

计数循环：把 `REPEAT` 与 `REND` 之间的内容重复执行指定次数。循环计数器是内置系统变量 `COUNT`（即 `COUNT:0`），从 0 开始，每轮 +1；`REPEAT` 不支持嵌套。

- 循环体内可用 `CONTINUE`（回到 `REPEAT` 处进入下一轮）和 `BREAK`（跳出循环）。
- 参数 ≤ 0 时不会执行循环体（解析期对常量 ≤ 0 给出警告）。
- 用 `GOTO` 等直接跳入 `REPEAT`～`REND` 内部时，会执行到 `REND` 之前然后忽略 `REND` 从下一行继续（eramaker 历史行为）。
- 退出循环（含 BREAK）时 `COUNT` 已被 +1，与一般语言的 for/break 不同（ecd/Difference.md）。
- 增强版为 `FOR`～`NEXT`（可自定义计数变量、初始值、步长）。

## 用法

### `REPEAT <次数>`
- `<次数>`：数值表达式，循环执行的次数。

```erb
REPEAT 10
    PRINTL 你好
REND
PRINTL

REPEAT 5
    PRINTFORML 分数：{COUNT*5}
REND
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:246`（`new REPEAT_Instruction(false)`，注释「RENDまで繰り返し。繰り返した回数がCOUNTへ。ネスト不可。」）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3098-3126`（类 `REPEAT_Instruction`，flag = `METHOD_SAFE | FLOW_CONTROL | PARTIAL`）；参数构造 `Runtime/Script/Statements/ArgumentBuilder.cs:1362-1376`；配对装配 `Runtime/Script/Loader/ErbLoader.cs:1019-1038`（REPEAT 压栈与嵌套检查）、`1225-1241`（REND 出栈配对）

```text
解析期（ArgumentBuilder，FunctionCode.REPEAT 分支）：
    若 COUNT 变量被禁止（getVarTokenIsForbid("COUNT")）：抛出 CodeEE（不能用 REPEAT）。
    若参数是常量且值 <= 0：警告「重复次数 ≤ 0」。
    cnt = 系统变量 COUNT 的 COUNT:0 项
    生成 SpForNextArgment(Cnt=COUNT:0, Start=0, End=参数, Step=1)
       （即计数变量固定 COUNT:0、初值固定 0、步长固定 1）

加载期（ErbLoader）：
    REPEAT 入嵌套栈；若栈中已有 REPEAT → 警告嵌套；
    若位于使用 COUNT:0 的 FOR 内 → 警告（变量冲突）。
    与 REND 配对：REPEAT.JumpTo = REND 行，REND.JumpTo = REPEAT 行。

执行期（REPEAT_Instruction.DoInstruction）：
    forArg = (SpForNextArgment)func.Argument
    func.LoopCounter = forArg.Cnt                 ; 即 COUNT:0
    func.LoopCounter.SetValue(forArg.Start.GetIntValue(exm), exm)   ; COUNT = 0
    func.LoopEnd   = forArg.End.GetIntValue(exm)  ; 次数
    func.LoopStep  = forArg.Step.GetIntValue(exm) ; 1
    若 (步长 > 0 且 终值 > COUNT) 或 (步长 < 0 且 终值 < COUNT)：
        return                                    ; 还有剩余次数 → 顺序执行循环体
    否则：
        state.JumpTo(func.JumpTo)                 ; 跳到 REND 行 → 下一行（循环结束）
```

## 备注

- ecd/Command.md 没有独立的 `### REPEAT` 小节，完整语义需综合 FOR 节、TRYGOTO 节（跳入循环的行为）与 Difference.md（COUNT +1）。
- `zh/ERB_File_Format.md:911-915` 给出了与 ecd 相同的语义（COUNT 计数、不支持嵌套、CONTINUE/BREAK 行为）。
- `zh/ERB_File_Format.md:913`「不支持嵌套」在源码中只是加载期**警告**而非编译错误；与文档略有出入。
- REPEAT 与 FOR 共用 `REPEAT_Instruction`（构造参数 `fornext`：false 时为 REPEAT，true 时为 FOR，且 FOR 才带 `EXTENDED` 标志与 `SP_FOR_NEXT` 参数解析器），因此 REND 的实现保留了负步长分支。
