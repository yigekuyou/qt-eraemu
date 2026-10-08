# VARI

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（Emuera 枚举成员 `Runtime/Script/Statements/BuiltInFunctionCode.cs:393`，位于文件的 `#region Emuera.NET`（第 392 行）；两套中文文档均未收录，语义据源码）
- **签名**（据 `LogicalLineParser` 的专用解析分支推定；`VARI` 不走通用参数构造器）：
  - `VARI <变量名>`：声明一维长度 1 的整数私有变量，初值 0
  - `VARI <变量名> = <整数表达式>`：声明并写入初值（长度仍为 1）
  - `VARI <变量名>, <长度1>` / `VARI <变量名>, <长度1>, <长度2>` / `VARI <变量名>, <长度1>, <长度2>, <长度3>`：声明 1/2/3 维数组（**不写初值**，全为 0）
- **实现状态**：**已实现，但默认关闭**。只有 `setting.json` 里 `UseScopedVariableInstruction` 为 `true`（UI 复选框「VAR系命令を利用可能にする」）时才注册进命令表；关闭时（默认）在 ERB 中写 `VARI`/`VARS` 会在解析期报「`VARI`命令は現在の設定では使用できません」（`Runtime/Script/Data/IdentifierDictionary.cs:661~665`，文案 `Runtime/Utils/EvilMask/Lang.cs:1200`）。
- **与 `VARSIZE`/`VARSET` 的关系**：**无关**。`VARSIZE`（`commands/VARSIZE.md`）/`VARSET`（`commands/VARSET.md`）是另外两个枚举成员，注册在 `Runtime/Script/Statements/FunctionIdentifier.cs:399` / `317`；ecd/ERB_Commands.md 表里只有 `VARSIZE`、`VARSET`、`CVARSET`，没有 `VARS`/`VARI`（`grep -rn "VARI\b" test/data/_extracted/` 零命中），因此不存在「ecd 里 VARS 是 VARSIZE 笔误」这种对应关系——`VARS`/`VARI` 是本仓库新增的独立命令。名字取自内置变量代码 `VariableCode.VAR`（`Runtime/Script/Statements/Variable/VariableToken.cs:2079`）/`VariableCode.VARS`（第 2278 行）。
- **文档来源**：无中文文档收录；ecd 只有 `VARSIZE`/`VARSET` 的章节（`ecd/docs/translation/Command.md:1158`、`:1293`），与本命令无关。依据源码：`Runtime/Script/Statements/FunctionIdentifier.cs:437~440`、`Runtime/Script/Statements/Instraction.Child.cs:34`、`Runtime/Script/Parser/LogicalLineParser.cs:423~493`、`Runtime/Script/Statements/Argument.cs:629`、`Runtime/Script/Statements/Variable/VariableToken.cs:2076`、`Runtime/Script/Statements/Variable/VariableData.cs:443`

## 语义

`VARI` 在**当前函数**里声明一个整数型的函数私有变量（长度 1 的标量，或 1~3 维数组），并可顺带写入初值。等价于「在函数内就地写下一条 `#DIM` + 赋值」，但：

- 作用域是**声明所在的函数标签**（`func.ParentLabelLine.AddPrivateVariable`），函数外不可见；与其他私有变量同名冲突时第二次声明被忽略（`Runtime/Script/Statements/LogicalLine.cs:288~297`）。
- 变量是**动态私有变量**（`Static = false`）：每次进入该函数时由 `ScopeIn` 重建数组（初值 0 / 空串），函数返回时 `ScopeOut` 归还外层数组，因此**不会**跨调用保留，也不进存档。
- **初值只在「一维且长度恰为 1」时写入**：执行到该行时先 `ScopeIn()`（重建数组），再判断 `GetLength(0) == 1`，是则把表达式值写进元素 `[0]`，否则（数组声明）什么都不做。
- 声明部分在**解析期**完成（变量在此后整段函数里都可按普通私有变量引用），执行期只负责「重建 + 写初值」。

行为边界（读码推定，文档未记载）：

- 数组形式（长度 >1 或 2/3 维）不会赋初值，这与 `VARSET` 不同；要填值请另写 `VARSET` 或逐个赋值。
- 二维以上且第一维长度恰为 1 的写法（如 `VARI X, 1, 5`）会走到「长度==1」分支，把只有 1 个元素的索引数组交给 2/3 维变量的 `SetValue`，访问 `arguments[1]` 时抛 `IndexOutOfRangeException`——未受控的 .NET 异常（`Process.handleException` 只能按「予期せぬエラー」报出）。
- 维度数字必须写成整数**字面量**：`int.Parse(leftSplit[i].Trim())` 失败会抛 `FormatException`（同样是未受控异常）。
- 变量名是裸标识符（`leftSplit[0].Trim()`），不能带索引、不能是表达式。
- 关闭该配置时不是「命令无效」而是**解析期错误**：`VARI`/`VARS` 在 `IdentifierDictionary.ThrowException` 中被特判成「当前设置下不可用」。

## 用法

### VARI <变量名> / VARI <变量名> = <整数表达式>

```erb
@TEST
  VARI COUNT              ; 声明标量，初值 0
  VARI TOTAL = 100        ; 声明标量并写初值
  COUNT += 1              ; 之后按普通私有变量使用
  PRINTFORML %TOTAL% / %COUNT%
```

### VARI <变量名>, <长度1>{, <长度2>{, <长度3>}}

```erb
@TEST
  VARI BUFFER, 10         ; 一维长度 10（初值全 0；不写初值）
  VARI MATRIX, 3, 3       ; 二维
  BUFFER:0 = 5
  MATRIX:1:2 = 7
```

### 开启方式

`setting.json`（与 Emuera.exe 同目录，`Runtime/Config/JSON/JSONConfig.cs:11`）：

```json
{ "UseScopedVariableInstruction": true }
```

或在配置对话框勾选「VAR系命令を利用可能にする」（`UI/Framework/Forms/ConfigDialog.cs:525/:1033` 读写该值，标签文本见 `ConfigDialog.resx:3484`）。

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:439`（`addFunction(FunctionCode.VARI, new VARI_Instruction())`，处在 `if (JSONConfig.Data.UseScopedVariableInstruction)` 与 `#region Emuera.NET`（第 436 行）之内，`VARS` 在第 440 行）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:393`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:34`（`VARI_Instruction`，`#region Emuera.NET VAR命令`，执行第 36~51 行，`CreateArgument` 第 53~56 行返回 null）
- 解析（参数在这里生成，不走 ArgumentBuilder）：`Runtime/Script/Parser/LogicalLineParser.cs:423~493`（VARI 分支）；参数对象 `Runtime/Script/Statements/Argument.cs:629`（`IntAsignArgument`）
- 变量实体：`Runtime/Script/Statements/Variable/VariableData.cs:443`（`CreatePrivateVariable`，动态私有变量分支第 493~517 行 → `PrivateInt1DVariableToken`，`Runtime/Script/Statements/Variable/VariableToken.cs:2076`）

```text
解析期 LogicalLineParser.ParseLine（LogicalLineParser.cs:423~493）:
    若标识符是 VARI 或 VARS（第 423 行）:
        line = InstructionLine(func)；line.ParentLabelLine = 当前函数标签（第 425~428 行）
        text = 去掉行尾 ';' 注释的原样文本（第 429~434 行）
        在第一个 '=' 处切成 left / right（第 436~447 行）；没有 '=' 时 right 为空
        leftSplit = left.Split(',')；varName = leftSplit[0].Trim()；lengths = [1]（第 449~451 行）
        —— VARI 分支（第 452~492 行）:
            若 leftSplit.Length > 1:                      # 数组声明
                lengths = leftSplit[1..] 逐个 int.Parse      # 失败 → FormatException（未受控）
            否则若 right 非空白:                            # 初值
                scaningLine = line
                exp = ReduceIntegerTerm(Analyse(right))      # 整数表达式
            varData = UserDefinedVariableData{ Name=varName, Static=false,
                                               Lengths=lengths, Dimension=lengths.Count, TypeIsStr=false }
            parentLine.AddPrivateVariable(varData)           # 注册到函数；同名已存在则忽略（LogicalLine.cs:288~297）
            若 exp != null → line.Argument = IntAsignArgument(varName, lengths, exp)
            否则          → line.Argument = IntAsignArgument(varName, lengths, SingleLongTerm(0))
            return line
            # 注：此分支在解析期就设好 line.Argument，因此运行期不会走 Instruction.CreateArgument（它返回 null）

执行期 VARI_Instruction.DoInstruction（Instraction.Child.cs:36~51）:
    arg = (IntAsignArgument)func.Argument
    varName = arg.ConstStr
    privateVar = func.ParentLabelLine.GetPrivateVariable(varName)   # 解析期注册的变量
    privateVar.ScopeIn()                    # 动态私有变量：新数组压栈并重建（PrivateInt*VariableToken.ScopeIn）
    若 privateVar.GetLength(0) == 1:        # 只有「长度恰为 1」的变量才写初值
        privateVar.SetValue(arg.Exp.GetIntValue(exm), [0])
    否则:                                    # 数组/多维：什么都不做（源码里是空 else 块）
        （空）

PrivateInt1DVariableToken（VariableToken.cs:2076~2142）:
    构造: 基类 VariableCode.VAR；IsStatic = false
    ScopeIn  (2123~2131): 旧数组入栈 → array = new long[sizes[0]]（初值 0）
    ScopeOut (2133~2141): 栈非空则弹回，否则置 null
    SetValue(long value, long[] arguments): array[arguments[0]] = value
```

## 备注

- **Emuera.NET 系私有扩展**：`VARI`/`VARS` 只在 `#region Emuera.NET`（`Runtime/Script/Statements/BuiltInFunctionCode.cs:392`、`Runtime/Script/Statements/FunctionIdentifier.cs:436`、`Runtime/Script/Statements/Instraction.Child.cs:33`）里出现，且受 `setting.json` 的 `UseScopedVariableInstruction` 控制；两套中文文档、EmueraEE readme/changelog、私家改造版 readme（Shift-JIS 转码后 grep）都没有记载。**未改造的原版 Emuera 1.824（仓库根 `Emuera/`，`Emuera/Properties/AssemblyInfo.cs:34` 为 `1.824.*`）里没有 `FunctionCode.VARI` 枚举与注册**（`grep -rn "FunctionCode.VARI" Emuera/` 零命中；`VARI`/`VARS` 在原版只作为变量代码 `VariableCode.VARS` 出现于 `Emuera/GameData/Variable/VariableCode.cs:277` 等处），故本命令是 emuera.em 新增而非原版命令。
- 命名辨析（任务提出的疑问）：**`VARI`/`VARS` 不是 `VARSIZE`/`VARSET` 的旧名或别名**。① 枚举成员不同（`Runtime/Script/Statements/BuiltInFunctionCode.cs:393/394` vs `122`(`VARSIZE`)/`239`(`VARSET`)）；② 注册处不同（`Runtime/Script/Statements/FunctionIdentifier.cs:439/440` vs `399`/`317`）；③ 语义不同（本命令是**声明**函数私有变量，`VARSIZE` 取数组长度、`VARSET` 批量赋值，见 `commands/VARSIZE.md`、`commands/VARSET.md`）；④ ecd 中 `VARS` 只作为 `VARSIZE`/`VARSET` 的子串出现，没有独立条目。
- 执行期「空 else」意味着数组声明期初值恒为 0，与 ecd 文档描述的私有变量默认值一致（`#DIM` 私有变量同样默认 0/空串），但本命令**不支持**像 `#DIM X,10 = 1,2,3` 那样给数组写初值。
- 变量是动态（`Static=false`）的：`LogicalLine.AddPrivateVariable` 会置 `hasPrivDynamicVar`，`Runtime/Script/Process.State.cs:411/487/506` 在进函数（含传参、事件函数切换）时调用 `ScopeIn()` 重置；因此 `VARI` 声明的变量不会像 `#DIM STATIC` 那样跨调用保留。
- 与之配对的字符串版见 `commands/VARS.md`。
