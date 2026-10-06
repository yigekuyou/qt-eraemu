# SIF

- **类别**：命令（单行条件结构）
- **签名**：`SIF <式>`（后接一行语句）
- **文档来源**：`ecd/docs/translation/ERB_Statements.md`（单行条件语句：SIF）、`ERB_File_Format.md`（条件判断：SIF）、`Difference.md`（`SIF` 的下一行是空行、注释行等的情况）；`Era-Chinese-Documentation/docs/ERB_File_Format.md`（# SIF 小节）、`Difference.md`

## 语义

SIF，即 Single if。条件表达式为真（非 0）时执行其下一行的语句，为假时跳过下一行继续。它等价于把下一行包进 `IF <式> ～ ENDIF` 的单行版本。

使用限制（下一行的合法性）：

- 下一行必须是普通可执行语句；不能是空行、注释行、标签（`$名称`）行，否则出错或失去意义。
- 下一行不能是 PARTIAL 类「部分指令」（如 `IF`、`SELECTCASE`、`DO` 等结构头）。
- 下一条语句与 SIF 不在同一物理行（中间隔了空行/注释行）时给出警告。

## 用法

### SIF <式> ⏎ <语句>

- `<式>`：数值表达式，非 0 为真。

```erb
SIF A > 0
	PRINTL A 是正数
```

等价于：

```erb
IF A > 0
	PRINTL A 是正数
ENDIF
```

更多示例：

```erb
A = 1
B = 2
C = 4
SIF A == 1
	PRINTL 测试1
SIF B != 1
	PRINTL 测试2
SIF C < 5
	PRINTL 测试3
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:236`（`new SIF_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3145`（`SIF_Instruction`，`SetJumpTo` 在 3153 行，`DoInstruction` 在 3179 行）

```text
SIF_Instruction:
    构造: ArgBuilder = INT_EXPRESSION, 标记 METHOD_SAFE | FLOW_CONTROL | PARTIAL | FORCE_SETARG

    SetJumpTo(useCallForm, func, currentDepth):        // 解析期检查下一行合法性
        jumpto = func.NextLine                          // SIF 的下一行
        if jumpto == null or jumpto.NextLine == null
           or jumpto 是函数标签行 or jumpto 是空行(NullLine):
            warn "SIF 的下一行不存在（空行/注释行/文件末尾）"        ; 不设跳转
            return
        elif jumpto 是指令行:
            sifFunc = jumpto
            if sifFunc.Function.IsPartial():            // 下一行是 IF/SELECTCASE/DO 等结构头
                warn "SIF 之后不能接 <指令名>"
            else:
                func.JumpTo = func.NextLine.NextLine    // 条件为假时的跳过目标 = 下下行
        elif jumpto 是 GOTO 标签行($标签):
            warn "SIF 之后不能接标签行"
        else:
            func.JumpTo = func.NextLine.NextLine

        if func.JumpTo != null 且 SIF 行号 + 1 != 下一行行号:
            warn(低级别) "SIF 之后有空行或注释行"

    DoInstruction(exm, func, state):                   // 执行期
        expArg = (ExpressionArgument)func.Argument
        if expArg.Term.GetIntValue(exm) == 0:          // 条件为假
            state.ShiftNextLine()                      // 跳过一行（与顺序执行到下下行等价）
        // 条件为真：什么也不做，自然落到下一行执行
```

## 备注

- 两套文档一致：SIF 只控制下一行。源码中「跳过」的实现是执行期 `ShiftNextLine()`，与「为真时顺序执行」结合，等价于单行 IF。
- ecd `Difference.md` 与 zh `Difference.md` 都记载：SIF 的下一行是空行、注释行等的情况会出错或失去意义；源码对此是分级处理——完全无下一行/标签行/结构头是 warning 级（Severity 2）且不建立跳过目标，隔行（中间有空行/注释）只是最低级别（Severity 0）警告，跳转仍然建立。即「SIF 下隔空行接语句」实际可用但会警告。
- SIF 的条件表达式在标志上含 `FORCE_SETARG`，即表达式求值可能写入 `RESULT`/`RESULTS`（与 IF 同机制）。
