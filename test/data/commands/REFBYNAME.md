# REFBYNAME

- **类别**：命令（Emuera 枚举成员 `Runtime/Script/Statements/BuiltInFunctionCode.cs:275`）
- **实现状态**：**已禁用（与 REF 双重失效）**。与 `REF` 共用同一个指令类，只是 `byname=true`：注册在 `Runtime/Script/Statements/FunctionIdentifier.cs:385`（`new REF_Instruction(true)`），执行体第一行同样是 `throw new NotImplCodeEE();`（`Runtime/Script/Statements/Instraction.Child.cs:2048`），即执行报「此功能在当前版本不可用」（`Runtime/Utils/EvilMask/Lang.cs:1053`）。但**实际上更早就会失败**：与 `REF` 共用的参数构造器 `SP_REF_ArgumentBuilder.CreateArgument` 的守卫条件恒真（`Runtime/Script/Statements/ArgumentBuilder.cs:2212~2214`），任何写法都在参数解析阶段被判「格式错误」并标记为错误行（详见 `commands/REF.md` 备注）。
- **签名**（历史签名，据 `SP_REF_ArgumentBuilder(byname=true)`，即 `FunctionArgType.SP_REFBYNAME`）：
  - `REFBYNAME <引用型变量>, <引用源变量名的字符串表达式>`
  - `REFBYNAME <引用型函数>, <引用源函数名的字符串表达式>`
- **文档来源**：两套中文文档均**未收录**本命令（`grep -rn "REFBYNAME" test/data/_extracted/` 无结果）。相关的仅有 ecd/zh 对「引用型变量」（`#DIM REF`）、`REF` 指令已禁用（ver1.815，`ecd/Version_Index.md:147`、`zh/Custom_Variable.md:262`）与按引用传参的记载（`ecd/Custom_Variable.md:254~270`、`ecd/Compatibility.md:143`）。语义依据源码（`Runtime/Script/Statements/FunctionIdentifier.cs:385`、`Runtime/Script/Statements/Instraction.Child.cs:2032~2110`、`Runtime/Script/Statements/ArgumentBuilder.cs:2198~2271`）

## 语义

`REFBYNAME` 是 `REF` 的「按名字（by name）绑定」版本：第二参数从标识符变成**字符串表达式**，可以在运行期算出要引用的变量名/函数名。除此之外与 `REF` 完全一致——包括它同样已被禁用。

规格对比（据参数构造器的差异）：

| | `REF` | `REFBYNAME` |
|---|---|---|
| 第 2 参数 | 标识符（变量名或 `#FUNCTION` 函数名），**装载期**由 `SP_REF_ArgumentBuilder` 查表解析成 `VariableToken`/`CalledFunction` | 字符串表达式，可运行期求值；常量时装载期解析，非常量时留到执行期用 `GetVariableToken`/`GetRefMethod` 查表 |
| 参数构造器 | `FunctionArgType.SP_REF`（`Runtime/Script/Statements/FunctionIdentifier.cs:384`） | `FunctionArgType.SP_REFBYNAME`（`Runtime/Script/Statements/FunctionIdentifier.cs:385`） |
| 执行体 | `REF_Instruction.DoInstruction`，`byname=false` | 同一个 `DoInstruction`，`byname=true` |
| 结果 | `RESULT` = 1（成功）/ 0（失败） | 同上 |

失败判定的历史语义（死代码）：源变量不存在、源是伪变量/常量、全局引用指向私有或局部变量、源是角色变量、整型与字符串型不一致、维数不一致（`ReferenceToken.MatchType`，`Runtime/Script/Statements/Variable/VariableToken.cs:514~539`；函数一侧为 `UserDefinedRefMethod.MatchType`，`Runtime/Script/Statements/Function/UserDefinedRefMethod.cs:33~65`，要求返回值与参数类型完全一致）→ 解绑并置 `RESULT = 0`。

## 用法

### REFBYNAME <引用型变量>, <源变量名字符串表达式>（本版执行即报错）

- `<引用型变量>`：`#DIM REF` / `#DIMS REF` 声明的引用型变量（或 ERH `#FUNCTION` 声明的引用型函数，见 `REF.md` 备注）。
- `<源变量名字符串表达式>`：字符串表达式；常量时装载期解析、非常量时执行期解析。

```erb
#DIM REF REFVAR, 0
#DIM SRCVAR, 10
#DIM SRCVAR2, 10

@TEST
  VARSET SRCVAR, 111
  VARSET SRCVAR2, 222
  REFBYNAME REFVAR, "SRCVAR"          ; 常量名
  ; PRINTFORML %REFVAR:0%  → 111（若 REF 可用的话）

  N = 2
  REFBYNAME REFVAR, "SRCVAR" + TOSTR(N)   ; 运行期拼名 → "SRCVAR2"
  ; 上述两种情况在本版执行时都会先因参数解析失败报「REFBYNAME命令:書式が間違っています」
  ; （参数构造器的守卫条件恒真），而不是「此功能在当前版本不可用」
```

本版可用的替代方案与 `REF` 相同：改用**按引用传参**（形参声明为 `#DIM REF` 变量），或在已知变量名的情况下直接写明变量——`REFBYNAME` 的「动态点名」能力目前没有替代命令。

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:385`（`addFunction(FunctionCode.REFBYNAME, new REF_Instruction(true))`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:275`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2032`（`REF_Instruction`，构造第 2034~2043 行——`byname` 决定 ArgBuilder 与 `this.byname`，执行第 2046~2110 行，**第 2048 行抛出**）
- 参数构造：`Runtime/Script/Statements/ArgumentBuilder.cs:2198`（`SP_REF_ArgumentBuilder`，第 2198~2271 行；`byname=true` 在第 2204 行生效）

```text
构造期 REF_Instruction(byname: true)（Instraction.Child.cs:2034~2043）:
    ArgBuilder = GetArgumentBuilder(FunctionArgType.SP_REFBYNAME)
    flag = METHOD_SAFE | EXTENDED

参数构造 SP_REF_ArgumentBuilder(byname: true).CreateArgument（ArgumentBuilder.cs:2209~2270）:
    wc.ShiftNext()（第 2212 行）
    第 1 项必须是标识符且其后必须是 ','，否则警告「格式错误」并返回 null（第 2213~2214 行）
        # ← 该条件恒真：ShiftNext 已落到逗号上，而逗号是 SymbolWord(Type=',')、
        #   IdentifierWord 的 Type 恒为 'A'，两个子条件必有一真 → 永远走这一支
    以下解析同样永不可达（详见 commands/REF.md 备注）
    第 2 项（byname=true，第 2218~2226 行）:
        name = ReduceExpressionTerm(wc, EoL)
        name 为 null / 整型 / 其后不是行尾 → 警告「格式错误」并返回 null
        name = name.Restructure(exm)
        若 name 是常量字面量 → srcCode = 它的字符串值（装载期即可解析）
    第 1 项解析（第 2234~2242 行）:
        refm = IdentifierDictionary.GetRefMethod(第 1 项)     # 引用型函数；本版恒为 null
        若 refm == null:
            token = GetVariableToken(第 1 项)；非引用型变量 → 警告「第 1 参数不是引用型变量」并失败
    组装（第 2244~2269 行）:
        refm != null → srcCode == null ? RefArgument(refm, name) : （查 ref 函数/非事件 #FUNCTION 标签，失败即警告）
        否则         → srcCode == null ? RefArgument(refVar, name)
                                    : srcVar = GetVariableToken(srcCode)
                                      srcVar 不存在 → 警告「变量未定义」并失败
                                      RefArgument(refVar, srcVar)

执行期 REF_Instruction.DoInstruction（Instraction.Child.cs:2046 起，byname 不影响流程）:
    第 2048 行: throw new NotImplCodeEE()        # ⇒ 后续历史实现均不可达
    -------------------------------------------------------------- 历史实现（永不执行）:
    str = arg.SrcTerm != null ? arg.SrcTerm.GetStrValue(exm) : null
        # REFBYNAME 恒走这一支：SrcTerm 就是第 2 参数（字符串表达式）
    若 arg.RefMethodToken != null:                        # 引用型函数
        若 str != null:
            srcRef = IdentifierDictionary.GetRefMethod(str)      # 运行期按名字查函数
            若 srcRef == null:
                label = LabelDictionary.GetNonEventLabel(str)
                若 label != null 且 label.IsMethod → call = CreateCalledFunctionMethod(label, str)
        否则若 SrcRefMethodToken != null → call = SrcRefMethodToken.CalledFunction
        若 call == null 或 !RefMethodToken.MatchType(call) → SetReference(null), RESULT = 0
        否则 → SetReference(call), RESULT = 1
        return
    refVar = arg.RefVarToken; srcVar = arg.SrcVarToken
    若 str != null:                                          # REFBYNAME 的运行期点名
        srcVar = IdentifierDictionary.GetVariableToken(str, null, true)   # 查不到则为 null
    若 srcVar == null 或 !refVar.MatchType(srcVar, false, out errmes):
        refVar.SetRef(null); RESULT = 0
    否则:
        refVar.SetRef((Array)srcVar.GetArray()); RESULT = 1
```

## 备注

- **本版不可用**（同 `REF`）：注册与参数解析保留，执行抛 `NotImplCodeEE`。两套中文文档连名字都没有收录，唯一相关记载是「`REF` 指令从 ver1.815 起不可用」（`ecd/Version_Index.md:147`、`zh/Custom_Variable.md:262`）——`REFBYNAME` 是同一族，随之一起失效；`grep -rn REFBYNAME` 在 ecd/zh 两套提取文档中零命中。
- 与 `REF` 的**唯一**差异就在参数构造器（`SP_REF` 对第 2 参数要求标识符、装载期定变量；`SP_REFBYNAME` 对第 2 参数要求字符串表达式、常量时可装载期定名、非常量时执行期 `GetVariableToken` 查名）与死代码里的 `str != null` 分支；指令类的执行流程、`RESULT` 约定、失败判定完全共享（`REF_Instruction` 单类，`Runtime/Script/Statements/Instraction.Child.cs:2032`）。
- `REFBYNAME` 依赖的两个基础设施在本版也都不完整：referenced-function（`UserDefinedRefMethod`）只能由 ERH 顶层 `#FUNCTION` 声明产生，而 `ErhLoader.analyzeSharpFunction` 已是 `throw new NotImplCodeEE()`（`Runtime/Script/Loader/ErhLoader.cs:385~391`，调用点 `Runtime/Script/Loader/ErhLoader.cs:115~117`），`IdentifierDictionary.AddRefMethod`（`Runtime/Script/Data/IdentifierDictionary.cs:454`）无任何调用者，`refmethodDic` 恒为空；所以即便 `REF` 系列解禁，引用型函数路径也仍然不能使用。
- 与原版对照：未改造的原版 Emuera 1.824（仓库根 `Emuera/`，`Emuera/Properties/AssemblyInfo.cs:34` 为 `1.824.*`）里 `REFBYNAME` 同样注册、执行即抛 `NotImplCodeEE`（`Runtime/Script/Statements/Instraction.Child.cs:1470` 的同一个类），但它的参数构造器写法正确（先取词再 `ShiftNext`，`Emuera/GameProc/Function/ArgumentBuilder.cs:1726~1730`）；emuera.em 改造版重写后条件恒真，于是本版连参数解析都过不去（默认报 `REFBYNAME命令:書式が間違っています`）。详见 `commands/REF.md` 备注。
- 参考文档：`commands/REF.md`（同族基名，含引用型变量定义、按引用传参替代方案与完整的死代码清单）。
