# CALLF

- **类别**：命令
- **签名**：
  - `CALLF <函数名>(, 参数1, 参数2……)`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO 系」→「CALLF `<字符串>` (, 参数1, 参数2……)」；`Era-Chinese-Documentation/docs/Custom_Expression.md`「`#FUNCTION(S)` 的调用」节（`CALLF`/`CALLFORMF` 是调用 `#FUNCTION(S)` 的专用指令）。

## 语义

以**忽略返回值**的方式调用用 `#FUNCTION`（或 `#FUNCTION #DIM` 等声明的式中函数）定义的函数。虽然目标必须是式中函数，但请以普通函数的参数书写格式调用；返回值被直接丢弃。因此除非被调用的式中函数内部修改了 `RESULT`/`RESULTS`（不推荐），这两个变量不会因此变化。

与 `CALL` 不同：`CALLF` 不改变流程状态（`METHOD_SAFE`，无 `FLOW_CONTROL`），不会压入新的脚本调用栈，而是当场求值。目标名字不存在时抛 CodeEE（指名不存在或不是 `#FUNCTION`）。`CALLFORMF` 是其 FORM 字符串版。

## 用法

### `CALLF <函数名>(, 参数1, 参数2……)`
- `<函数名>`：字符串常量，对应 `@函数名` 且该函数以 `#FUNCTION` 声明。
- `参数1, 参数2……`：按函数声明的形参传入（与普通函数相同的书写格式）。
```erb
@INIT_COUNTERS
#FUNCTION
;把某些全局数组清零等副作用处理
RETURN 0

@EVENTFIRST
  CALLF INIT_COUNTERS
  ;执行完毕，返回值被丢弃
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:352`（`new CALLF_Instruction(false)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1153`（`CALLF_Instruction`，构造参数 `form=false`）

```text
指令类 CALLF_Instruction(form):
    form=false → 参数构造器 SP_CALLF（函数名为常量字符串 + 实参行）
    flag = EXTENDED | METHOD_SAFE | FORCE_SETARG   # 无 FLOW_CONTROL：不改流程

    SetJumpTo(...):                     # 编译期解析
        若参数非常量 → useCallForm = true（运行期解析）
        否则:
            callfArg.FuncTerm = IdentifierDictionary.GetFunctionMethod(
                                    LabelDictionary, ConstStr, RowArgs, true)
            解析抛出 CodeEE → 记为警告（级别2）
            FuncTerm == null → 警告"函数不存在"（AnalysisMode 下只记名字）

    DoInstruction(exm, func, state):
        若 (!参数为常量) 或 exm.Console.RunERBFromMemory:
            # 动态解析（CALLFORMF 路径 / 内存执行模式）
            labelName = FuncnameTerm.GetStrValue(exm)
            mToken = IdentifierDictionary.GetFunctionMethod(
                        LabelDictionary, labelName, RowArgs, true)
        否则:
            labelName = func.Argument.ConstStr
            mToken = callfArg.FuncTerm       # 用编译期缓存
        若 mToken == null:
            抛 CodeEE（"调用了不存在的式中函数" + labelName）
        mToken.GetValue(exm)                 # 当场求值，返回值直接丢弃
```

## 备注

- `CALLFORMF` 是同一实现类的 `form=true` 变体（`Runtime/Script/Statements/FunctionIdentifier.cs:352`），函数名可为 FORM 格式文本。
- ecd 文档强调「请用普通函数的参数格式调用」，即实参不是式中函数的括号求值语法而是逗号分隔参数表——这与参数构造器 `SP_CALLF` 的解析方式一致。
- 文档说「除非被调函数内部修改 RESULT/RESULTS，否则它们不变」：由于 `CALLF` 不经过 `IntoFunction`/`Return` 流程，`RESULT` 不会被隐式赋值，与文档一致。
- zh 文档（`Custom_Expression.md`）仅概括 `CALLF`/`CALLFORMF` 为调用 `#FUNCTION(S)` 的专用指令，无错误行为描述；错误语义以源码为准。
