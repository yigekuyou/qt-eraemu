# REF

- **类别**：命令（Emuera 枚举成员 `Runtime/Script/Statements/BuiltInFunctionCode.cs:274`）
- **实现状态**：**已禁用（而且是双重失效）**。名字仍注册（`Runtime/Script/Statements/FunctionIdentifier.cs:384`），参数构造器、参数类、引用型变量（`#DIM REF`）都还在，但：
  1. 参数构造器里的守卫条件写错了，任何写法都会走进「格式错误」分支（`Runtime/Script/Statements/ArgumentBuilder.cs:2212~2214`，详见「备注」），行被标记为错误行；
  2. 指令执行体第一行就是 `throw new NotImplCodeEE();`（`Runtime/Script/Statements/Instraction.Child.cs:2048`），即执行到该行报「此功能在当前版本不可用」（`Runtime/Utils/EvilMask/Lang.cs:1053` 的 `CanNotUseFuncCurrentVer`，`NotImplCodeEE` 继承 CodeEE，见 `Runtime/Utils/EmueraException.cs:73~81`）；抛出点之后是一整段「永不执行」的历史实现（`Runtime/Script/Statements/Instraction.Child.cs:2050~2109`）。
  ecd 的版本表把这次移除记为 **1.815**（`ecd/docs/translation/Version_Index.md:147`「`REF` 指令不再可用」）。
- **签名**（历史签名，据 `SP_REF_ArgumentBuilder`；本版装载不报错，但该行一旦被解析/执行必定报错）：
  - `REF <引用型变量>, <引用源变量名>`
  - `REF <引用型函数>, <引用源函数名>`
  - （函数引用一侧还需 ERH 顶层 `#FUNCTION` 声明，见「备注」——该声明路径同样已抛 NotImplCodeEE）
- **文档来源**：两套中文文档都**没有 REF 指令的独立小节**，只有散落提及：`ecd/docs/translation/Custom_Variable.md:254~270`（`#DIM REF HOGE1DIM,0` 等定义、「引用型变量没有实体……通过 `REF` 指令（从 ver1.815 起不可用）或按引用传递进来的变量」）、`ecd/Version_Index.md:147`（1.815 移除）、`ecd/Compatibility.md:143`（ver1.810 起支持按引用传参）、`ecd/Terminology.md:174`（术语「引用型变量」）、`ecd/Error_Index.md:460~461`（`REF引数は…` 两条错误，指函数形参的引用传递，与本指令无关）。zh 侧共 14 行提到 `REF`（`grep -rn REF test/data/_extracted/zh/ | wc -l`）：`zh/Custom_Variable.md:239/244~249/262/270`（其中第 262 行明确「`REF` 指令（从 ver1.815 开始不可用）」）、`zh/Variable.md:663/786`、`zh/Function_and_Preprocessor.md:353`、`zh/Header_File.md:120`。语义主体来自源码（含那段死代码）。

## 语义

`REF` 是「把引用型变量绑定到某个实体变量（或把引用型函数绑定到某个 `#FUNCTION` 函数）」的指令。引用型变量本身没有实体数组（`ReferenceToken.GetArray()` 在未绑定时抛 `EmptyRefVar` 错误），对它读写就是透过引用操作目标变量。

历史语义（读死代码得出，这一行为**本版已不可用**）：

- `<引用型变量>` 必须是 `#DIM REF` / `#DIMS REF` 声明的引用型变量（`ReferenceToken`，`Runtime/Script/Statements/Variable/VariableToken.cs:422`），或 ERH `#FUNCTION` 声明的引用型函数（`UserDefinedRefMethod`）。
- `<引用源变量名>` 是**标识符**（不是字符串表达式，这是与 `REFBYNAME` 的唯一区别）；也可以写成引用型函数名。
- 运行结果写入 `RESULT`：绑定成功 1，失败 0。失败包括：源变量不存在、源是伪变量（`IsCalc`）、源是常量、全局引用指向私有/局部变量、源是角色变量（`allowChara=false`）、整型/字符串型不一致、维数不一致（`ReferenceToken.MatchType`，`Runtime/Script/Statements/Variable/VariableToken.cs:514~539`）。
- 绑定成功后引用变量获得源变量的**数组本体**（`SetRef((Array)srcVar.GetArray())`），此后对引用变量的读写会直接影响源变量；解绑/失败时 `SetRef(null)`。

错误行为：本版不存在「历史语义」的执行机会。**实际上首先撞上的是参数解析失败**——`SP_REF_ArgumentBuilder` 的检验条件恒真（见「备注」），所以 ERB 里的 `REF …` 行会被标记为错误行，默认配置下在执行该行时抛 `CodeEE`，报的是 `REF命令:書式が間違っています`（`WrongFormat`，拼装见 `Runtime/Script/Statements/ArgumentBuilder.cs:30~40`；抛出点 `Runtime/Script/Process.ScriptProc.cs:40~45`）；只有在参数解析侥幸通过的前提下才会执行到 `DoInstruction`，从而抛 `NotImplCodeEE`（"此功能在当前版本不可用"）。两者都是 `CodeEE`，运行期由 `Process.handleException` 按普通脚本错误处理：打印出错文件名/行号与原命令行并停止执行（`Runtime/Script/Process.cs:506~539`）；TRYC 系只拦截「函数不存在」，不会吞掉这类异常。

## 用法

### REF <引用型变量>, <引用源变量名>（本版执行即报错）

- `<引用型变量>`：`#DIM REF`/`#DIMS REF` 声明的变量名。
- `<引用源变量名>`：变量标识符（可带维度声明时的名字，不带索引）。

```erb
#DIM REF REFVAR, 0        ; 声明一维引用型数值变量（仍可正常声明）
#DIM SRCVAR, 10

@TEST
  SRCVAR:0 = 123
  REF REFVAR, SRCVAR      ; ← 本版执行到这里先报「REF命令:書式が間違っています」
                          ;   （参数构造器守卫恒真）；即便解析通过，
                          ;   执行体也会抛「此功能在当前版本不可用」
  PRINTFORML %REFVAR:0%   ; REF 不可用，只能改用引用传参
```

现在要用引用语义，请改用**按引用传参**（ver1.810 起支持，见 ecd `Custom_Variable.md`:270 与 `Compatibility.md`:143）：把形参声明成引用型变量，调用时传入实参变量。

```erb
@SETONE(REF TARGET)
  TARGET:0 = 1

@MAIN
  SRCVAR:0 = 0
  SETONE(SRCVAR)          ; 形参是引用型变量 → 实参按引用传入
  PRINTFORML %SRCVAR:0%   ; → 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:384`（`addFunction(FunctionCode.REF, new REF_Instruction(false))`；`REFBYNAME` 是同一类的 `true` 版，第 385 行）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:274`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2032`（`REF_Instruction`；构造第 2034~2043 行，执行第 2046~2110 行，**第 2048 行抛出**）
- 参数构造：`Runtime/Script/Statements/ArgumentBuilder.cs:2198`（`SP_REF_ArgumentBuilder`，第 2198~2271 行）；参数对象 `Runtime/Script/Statements/Argument.cs:545`（`RefArgument`）

```text
构造期 REF_Instruction(byname)（Instraction.Child.cs:2034~2043）:
    ArgBuilder = GetArgumentBuilder(byname ? SP_REFBYNAME : SP_REF)
    flag = METHOD_SAFE | EXTENDED

参数构造 SP_REF_ArgumentBuilder.CreateArgument（ArgumentBuilder.cs:2209~2270）:
    # 构造器 : argumentTypeArray = [void, void]; minArg = 2; this.byname = byname（第 2200~2205 行）
    wc = 参数区字词的 WordCollection（popWords，第 2211 行）
    wc.ShiftNext()                              # 此时 Current 已是第 1 个词，这一步跳到了逗号上
    if (!(wc.Current is IdentifierWord id) || wc.Current.Type != ',')   # ← 条件恒真（第 2213 行）
        { warn(WrongFormat, level 2) ⇒ line.IsError = true; return null }   # 恒走这一支
    ------- 以下解析全部不可达（守卫恒真）-------
        第 2 项（byname=true）: name = ReduceExpressionTerm(EoL)
            不是字符串表达式（整型或空）或其后不是行尾 → 警告「格式错误」并返回 null
            name 是常量字面量时 → srcCode = 字符串值
        第 2 项（byname=false）: 必须是标识符且其后行尾 → srcCode = 标识符名
        refm = IdentifierDictionary.GetRefMethod(id.Code)   # 引用型函数（ERH #FUNCTION 声明；本版恒为 null）
        若 refm == null:
            token = GetVariableToken(id.Code)
            token == null 或 !token.IsReference → 警告「第 1 参数不是引用型变量」并返回 null
            refVar = (ReferenceToken)token
        若 refm != null:                                    # 引用型函数路径
            srcCode == null → RefArgument(refm, name)
            srcCode 对应另一个 ref 函数 → RefArgument(refm, srcRef)
            否则找非事件函数标签；不存在 / 非 #FUNCTION → 警告后失败
            成功 → RefArgument(refm, CalledFunction.CreateCalledFunctionMethod(label, name))
        否则:                                                # 引用型变量路径
            srcCode == null → RefArgument(refVar, name)
            srcVar = GetVariableToken(srcCode)；不存在 → 警告「变量未定义」后失败
            RefArgument(refVar, srcVar)

执行期 REF_Instruction.DoInstruction（Instraction.Child.cs:2046~2110）:
    第 2048 行: throw new NotImplCodeEE()        # ⇒ 以下全为不可达代码（编译器会提示 CS0162）
    -------------------------------------------------------------- 历史实现（永不执行）:
    arg = (RefArgument)func.Argument
    str = arg.SrcTerm 非空 ? arg.SrcTerm.GetStrValue(exm) : null      # REFBYNAME 的非定名情况
    若 arg.RefMethodToken != null:                # —— 引用型函数分支
        srcRef = arg.SrcRefMethodToken；call = arg.SrcCalledFunction
        若 str != null:                            # REFBYNAME 且第 2 参数不是常量
            srcRef = IdentifierDictionary.GetRefMethod(str)
            若 srcRef == null:
                label = LabelDictionary.GetNonEventLabel(str)
                若 label != null 且 label.IsMethod:
                    call = CalledFunction.CreateCalledFunctionMethod(label, str)
        否则若 srcRef != null:
            call = srcRef.CalledFunction
        若 call == null 或 !RefMethodToken.MatchType(call):   # 返回值/参数型必须完全一致（UserDefinedRefMethod.cs:33~65）
            RefMethodToken.SetReference(null); RESULT = 0
        否则:
            RefMethodToken.SetReference(call);  RESULT = 1
        return
    否则:                                          # —— 引用型变量分支
        refVar = arg.RefVarToken；srcVar = arg.SrcVarToken
        若 str != null:                            # REFBYNAME 且第 2 参数不是常量
            srcVar = IdentifierDictionary.GetVariableToken(str, null, true)
        若 srcVar == null 或 !refVar.MatchType(srcVar, allowChara: false, out errmes):
            refVar.SetRef(null); RESULT = 0
        否则:
            refVar.SetRef((Array)srcVar.GetArray()); RESULT = 1
        return
```

## 备注

- **本版（Emuera 1.824+v18+EMv17+EEv41 系）中 REF 已彻底不可用**：注册保留、执行抛 `NotImplCodeEE`（`Runtime/Script/Statements/Instraction.Child.cs:2048`）。**未改造的原版 Emuera 1.824 同样如此**（仓库根 `Runtime/Script/Statements/Instraction.Child.cs:1470` 的 `REF_Instruction`，第 1486 行同一个 `throw new NotImplCodeEE();`），可见「禁用」是上游自 1.815 起的既定事实，不是 EM/EE 改造造成的；改造版只是在参数构造器上又坏了一层（见下条）。删除时间点见 ecd `Version_Index.md:147`（1.815），zh 文档 `Custom_Variable.md:262` 也一致记载「从 ver1.815 开始不可用」。
- **参数检验条件恒真（读码推定，本仓库无法运行 Emuera 实测）**：`Runtime/Script/Statements/ArgumentBuilder.cs:2212~2214` 写作
  ```text
  wc.ShiftNext();
  if (!(wc.Current is IdentifierWord id) || wc.Current.Type != ',') { warn(WrongFormat, 2); return null; }
  ```
  ① `WordCollection` 在一次 `Analyse` 之后 `Current` 已指向**第一个**词（构造与 `Add` 见 `Runtime/Script/Parser/WordCollection.cs:13~17/30~34`，"Current 就是首词" 也从 `Runtime/Script/Statements/Expression/ExpressionParser.cs:127` 的 `wc.Current.Type == ','`、`Runtime/Script/Statements/ArgumentBuilder.cs:737/782` 的用法可交叉印证），所以这次 `ShiftNext()` 落在了逗号上；
  ② `IdentifierWord.Type` 恒为 `'A'`（`Runtime/Script/Parser/Word.cs:25~31`），逗号是 `SymbolWord(',')`（`Runtime/Script/Parser/LexicalAnalyzer.cs:927`、`Runtime/Script/Parser/Word.cs:74~80`，`Type` 即字符本身），因此 `!(… is IdentifierWord)` 与 `Type != ','` 必有其一为真，条件**恒成立**。
  ③ **与未改造的上游对照，可确认这是 emuera.em 改造版在重写时引入的回归**：仓库根目录保留着原版 Emuera 1.824 源码（`Emuera/`，`Emuera/Properties/AssemblyInfo.cs:34` 为 `1.824.*`，无 EM/EE 区域），其同名构造器写法正确——先取词再前进：
  ```csharp
  IdentifierWord id = wc.Current as IdentifierWord;   // Emuera/GameProc/Function/ArgumentBuilder.cs:1727
  wc.ShiftNext();
  if (id == null || wc.Current.Type != ',') { warn("書式が間違っています", line, 2, false); return null; }  // :1729
  ```
  改造版把前置捕获合并进判断式后顺序颠倒，条件随之恒真。
  ④ 结果：任何 `REF`/`REFBYNAME` 行都在参数解析阶段被判为格式错误。消息由 `warn()` 拼成 `命令名 + "命令:" + 文本`（`Runtime/Script/Statements/ArgumentBuilder.cs:30~40`、`Runtime/Utils/EvilMask/Lang.cs:663`），即 **`REF命令:書式が間違っています`**（`WrongFormat`，`Runtime/Utils/EvilMask/Lang.cs:675`），level 2 ⇒ `line.IsError = true`。默认配置（`ロード時に引数を解析する = NO`）下参数解析发生在执行该行时（`Runtime/Script/Process.ScriptProc.cs:40~45`），所以使用者看到的是这条格式错误；只有开启该配置或解析模式时才会在装载期就以警告形式报出。第 2048 行的 `NotImplCodeEE` 在这段守卫被修好之前实际不可达——这与「REF 在 1.815 被禁用」的结论并不矛盾，只是本改造版禁用得更彻底（原版是「解析通过、执行即抛」）。
- 引用型变量**定义**（`#DIM REF X,0`）与**按引用传参**仍然完全可用（`Runtime/Script/Data/UserDefinedVariable.cs:68/82` 解析 `REF` 关键字；`VariableData.CreatePrivateVariable` 第 446 行起的 `data.Reference` 分支构造 `ReferenceInt/Str?DToken`；`Runtime/Script/Process.State.cs:487~506` 在传参时 `SetRef`）。被禁掉的只是「用 REF 指令事后绑定」这条路。ecd/zh 的措辞正是这个意思（「操作引用型变量实际上就是操作通过 REF 指令（已不可用）或按引用传递进来的变量」）。
- 引用型函数（`REF` 的第一参数为函数）这条支路在本版**双重不可用**：一是 `REF` 执行即抛；二是它依赖的 `UserDefinedRefMethod` 只能由 ERH 顶层的 `#FUNCTION`/`#FUNCTIONS` 声明注册（`Runtime/Script/Loader/ErhLoader.cs:115~117` → `analyzeSharpFunction`，`Runtime/Script/Loader/ErhLoader.cs:385~391`），而该函数体内第一行就是 `throw new NotImplCodeEE();`，历史注册代码 `idDic.AddRefMethod(...)` 已被注释掉；`IdentifierDictionary.AddRefMethod`（`Runtime/Script/Data/IdentifierDictionary.cs:454`）在全仓没有任何调用点，`refmethodDic` 永远是空表。
- 与 `REFBYNAME` 的关系：两者是**同一个类** `REF_Instruction` 的两个实例，差异仅在参数构造器（`SP_REF` 要求第 2 参数是标识符；`SP_REFBYNAME` 要求第 2 参数是字符串表达式）和死代码里 `str != null` 的取名字路径。执行体完全相同（同样第一天就抛）。详见 `commands/REFBYNAME.md`。
- 本命令**不在** `test/data/emuera_standard_cmds.txt` 的导出清单里（导出脚本的漏收，见 `test/data/README.md` 的「已知问题：导出脚本漏收 88 个枚举成员」），但它在 `Runtime/Script/Statements/BuiltInFunctionCode.cs:274`、`Runtime/Script/Statements/FunctionIdentifier.cs:384` 都有正式登记，属标准枚举成员而非 EM/EE 私有扩展。
