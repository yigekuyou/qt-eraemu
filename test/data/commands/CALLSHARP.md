# CALLSHARP

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（Emuera 枚举成员 `Runtime/Script/Statements/BuiltInFunctionCode.cs:169`；两套中文文档均未收录，语义据源码）
- **签名**（从 `SP_CALLSHARP_ArgumentBuilder` 推定的完整形式；函数名部分是**字符串字面量**，不是表达式，也不是 FORM 文本）：
  - `CALLSHARP <C# 方法名>()`
  - `CALLSHARP <C# 方法名>, 参数1, 参数2, …`
  - `CALLSHARP <C# 方法名>(参数1, 参数2, …)`
  - `CALLSHARP <C# 方法名>[下标]`（带下标的写法也能解析，但见「备注」——行内不使用该下标）
- **文档来源**：无中文文档收录（ecd 的 `CALL·JUMP·GOTO 系命令辅助选择器` 一节里没有 CALLSHARP；`grep -rn CALLSHARP test/data/_extracted/ecd/ test/data/_extracted/zh/` 无结果）。依据源码：`Runtime/Script/Statements/FunctionIdentifier.cs:354`、`Runtime/Script/Statements/Instraction.Child.cs:1212`、`Runtime/Script/Statements/ArgumentBuilder.cs:922`、`Runtime/Utils/PluginSystem/`；示例用法见 `emuera.em/EmueraPluginExample/Plugin.cs:26`（插件作者编写的 ERB 示例）

## 语义

调用一个由**外部 C# 插件**（`Plugins` 目录下的 DLL）注册的方法，即「CALL + SHARP（C#）」：与 `CALL` 的目标是 ERB 函数标签相对，`CALLSHARP` 的目标是插件里实现 `IPluginMethod` 的 C# 方法，按**方法名**在 `PluginManager` 的方法表里查找。

要点：

- 方法名必须写成**字面量字符串**（`SP_CALLSHARP_ArgumentBuilder` 一律构造 `SingleStrTerm`，因此 `IsConst` 恒为 true），不能写成表达式或 FORM 格式文本——这一点与 `CALLFORM`/`CALLF` 不同。
- 参数按位置逐个传给 C# 方法，实参可以是任意表达式；**若某个实参是变量（`VariableTerm`），调用返回后该变量的值会被 C# 侧写入的 `PluginMethodParameter` 值回写（即引用/输出参数语义）**；常量或表达式实参则忽略返回值。
- 参数类型只有两种：字符串或整数，由 ERB 侧表达式的类型决定（`PluginMethodParameterBuilder.ConvertTerm`）；没有类型校验，类型不匹配由插件自己承担。
- 因为 flag 含 `FORCE_SETARG`，命令行在**装载期**就完成参数解析，并在装载期用方法名查表：
  - 查不到方法 → `ParserMediator.Warn(MethodNotFound, 级别 2, isError=true)`，该行被标记为错误行（`line.IsError = true`，见 `Runtime/Script/Data/ParserMediator.cs:106~117`），运行时执行到该行会抛 `CodeEE(line.ErrMes)`（`Runtime/Script/Process.ScriptProc.cs:32~33`）。
  - 查得到 → 把 `IPluginMethod` 缓存在 `arg.CallFunc`。
- 副作用取决于插件；插件也可以通过 `PluginManager.ExecuteLine` 反向执行 ERB 命令。
- 什么时候会警告插件可用：容器启动时若 `Plugins` 目录下存在 DLL，且配置项「外部プラグインが有効時に警告を表示する」(`PluginAvailableWarn`, 默认 true) 为真，会打印一行提示「注意：外部プラグイン功能已启用，由此产生的问题不在 Emuera 支持范围内」（`Runtime/Script/Process.cs:198~200`、`Runtime/Utils/EvilMask/Lang.cs:1328`）。
- 插件被有意设计为「危险功能」：源码里多处留有 `#region EE_CALLSHARP注意` 标记（`GlobalStatic.cs:52`、`Runtime/Config/Config.cs:137/620`、`Runtime/Config/ConfigCode.cs:158`、`Runtime/Config/ConfigData.cs:136`），提示使用该功能需自行承担风险。

## 用法

### CALLSHARP <方法名>(参数1, 参数2, …)

- `<方法名>`：插件中 `IPluginMethod.Name` 返回的名字（大小写敏感与否取决于 `IgnoreCase` 配置，`PluginManager.HasMethod`/`GetMethod` 会按配置统一大小写）。
- 参数：任意个数的整数/字符串表达式。

```erb
; emuera.em/EmueraPluginExample/Plugin.cs 第 25~29 行给出的官方示例（插件作者的 ERB 示例文本）
#DIMS OUT_VAR_TEST = ""
CALLSHARP HelloWorld()
CALLSHARP ParametersAndReferences("This line was passed from ERB!", OUT_VAR_TEST)
PRINTFORML %OUT_VAR_TEST%
CALLSHARP ERBExecutionExample()
```

上面第二次调用展示了回写语义：`OUT_VAR_TEST` 是变量实参，插件把 `args[1].strValue` 改成新字符串后，ERB 侧的 `OUT_VAR_TEST` 随之改变（`Runtime/Script/Statements/Instraction.Child.cs:1264~1279`）。

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:354`（`addFunction(FunctionCode.CALLSHARP, new CALLSHARP_Instruction())`，同组还有 `CALLF`/`CALLFORMF`，见第 352~353 行）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:169`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1212`（`CALLSHARP_Instruction`；构造第 1214~1218 行、`SetJumpTo` 第 1237~1254 行、`DoInstruction` 第 1256~1280 行）
- 参数构造：`Runtime/Script/Statements/ArgumentBuilder.cs:922`（`SP_CALLSHARP_ArgumentBuilder`，第 922~984 行）；参数对象 `Runtime/Script/Statements/Argument.cs:287`（`SpCallSharpArgment`）
- 插件侧：`Runtime/Utils/PluginSystem/PluginManager.cs:20`（`LoadPlugins` 第 271 行从 `<exe 目录>/Plugins/*.dll` 装载、`GetMethod` 第 321 行、`HasMethod` 第 331 行、`ExecuteLine` 第 45 行）、`Runtime/Utils/PluginSystem/PluginMethodParameter.cs:27`（`ConvertTerm`）

```text
构造期 CALLSHARP_Instruction():
    ArgBuilder = GetArgumentBuilder(FunctionArgType.SP_CALLCSHARP)
    flag = EXTENDED | METHOD_SAFE | FORCE_SETARG
    # 注意：类里还重写了一个 CreateArgument（第 1220~1235 行，用 getStBar 造整行横条字符串），
    #      但 ArgBuilder 非 null 时 ArgumentParser 只会调用 ArgBuilder（ArgumentParser.cs:27~30），
    #      该重写是 BAR 系复制粘贴残留的死代码。

参数构造 SP_CALLSHARP_ArgumentBuilder.CreateArgument（ArgumentBuilder.cs:928~983）:
    st = line.PopArgumentPrimitive()
    str = ReadString(st, 以 '(' '[' ',' ';' 为界)（第 932 行）; str = str.Trim(' ', '\t')
    funcname = new SingleStrTerm(str)                     # 方法名恒为字面量
    wc = Analyse(st, EoL)                                 # 其余参数部分
    若当前字符是 '['（第 941~950 行）:
        subNames = ReduceArguments(wc, ']')                # 下标（解析但运行期不使用）
        若其后不是行尾且不是 '(' → 跳过一项；再解析 args
    若当前字符是 '(' 或 ','（第 951~959 行）:
        '(' → args = ReduceArguments(wc, ')')
        ',' → args = ReduceArguments(wc, EoL)；其后必须行尾，否则警告「格式错误」并返回 null
    subNames/args 为 null 时置为空列表；逐项 Restructure(exm)
    ret = new SpCallSharpArgment(funcname, subNames, args)
    funcname 是常量（恒真）→ ret.IsConst = true; ret.ConstStr = 方法名
        ConstStr == "" → 警告「没有指定函数名」并返回 null（第 976~980 行）

装载期 SetJumpTo（Instraction.Child.cs:1237~1254）:
    若 !func.Argument.IsConst:                # 构造器保证恒为常量，此分支实际不可达
        useCallForm = true; return            # 该行被当作「调用形式未知」→ 放弃函数未调用检查
    arg = (SpCallSharpArgment)func.Argument
    manager = PluginManager.GetInstance()
    若 !manager.HasMethod(arg.ConstStr):
        ParserMediator.Warn(string.Format(MethodNotFound, arg.ConstStr), func, 2, isError: true)
        # MethodNotFound 文本见 Lang.cs:1204；isError=true ⇒ line.IsError = true
        return                                # arg.CallFunc 保持 null
    arg.CallFunc = manager.GetMethod(arg.ConstStr)       # 装载期缓存 IPluginMethod

执行期 DoInstruction（Instraction.Child.cs:1256~1280）:
    arg = (SpCallSharpArgment)func.Argument
    pluginArgs[i] = PluginMethodParameterBuilder.ConvertTerm(arg.RowArgs[i], exm)   # 逐个求值
        # 表达式是字符串型 → new PluginMethodParameter(string)
        # 否则              → new PluginMethodParameter(long)
    arg.CallFunc.Execute(pluginArgs)                     # 调用 C# 方法（可任意副作用）
    回写：对每个 rowArg 是 VariableTerm 的实参 i:
        字符串型变量 → rowArg.SetValue(pluginArgs[i].strValue, exm)
        否则        → rowArg.SetValue(pluginArgs[i].intValue, exm)

插件装载（PluginManager.cs:271~319）:
    若 <exe 目录>/Plugins 不存在 → 直接返回（不自动创建目录）
    逐个 Assembly.LoadFrom(Plugins/*.dll)，找类型名 "PluginManifest" 的类
    实例化后 GetPluginMethods() → AddMethod() 注册进方法表
    存在 dll → GlobalStatic.ExistPlugin = true（用于启动提示）
```

## 备注

- **两套中文文档都没有 CALLSHARP**，也没有任何中文资料描述 Emuera 插件机制；本文件语义完全基于源码。它是**本仓库（Emuera.NET / emuera.em，含 EM+EE 改造）的私有扩展**：① `CALLSHARP` 及整个 `Runtime/Utils/PluginSystem/` 在**未改造的原版 Emuera 1.824（仓库根 `Emuera/`，`Emuera/Properties/AssemblyInfo.cs:34` 为 `1.824.*`）中完全不存在**（`grep -rl "CALLSHARP" Emuera/` 与 `find Emuera -name PluginManager.cs` 均零命中）；② `EmueraPluginExample` 工程（`emuera.em/EmueraPluginExample/Plugin.cs`）是该功能的配套示例，插件必须在 Emuera 解决方案内引用编译。
- 与 `CALL` 的差异（`#`/Sharp 语义）：`CALL` 目标为 ERB 标签且由 `CalledFunction.CallFunction` 解析（`Runtime/Script/Process.CalledFunction.cs:109`），`CALLSHARP` 目标为 `Plugins/*.dll` 里的 C# 方法、经 `PluginManager` 按名查表；`CALL` 会用 `IntoFunction` 压入 ERB 调用栈，`CALLSHARP` 直接在 C# 里执行、不进入 ERB 栈。flag 上两者都带 `FORCE_SETARG`，但 `CALL` 带 `FLOW_CONTROL`、`CALLSHARP` 带 `METHOD_SAFE | EXTENDED`（可在 `#FUNCTION` 式中函数里调用）。
- 「下标记法」`CALLSHARP Foo[SUB]` 会被解析进 `arg.SubNames`，但 `DoInstruction` 只使用 `arg.RowArgs`，下标被忽略；估计是从 `CALLF` 的下标语法沿用而来。
- 装载期 `SetJumpTo` 里 `if (!func.Argument.IsConst)` 的分支实际不可达（构造器恒置 `IsConst=true`），因此不存在「运行期才知道方法名」的路径；运行期不会出现 `GetMethod` 的 `KeyNotFoundException`，方法缺失一律在装载期变成错误行。
- `MethodNotFound` 的文案写成 `"[0]"メソッドが見つかりません`（`Runtime/Utils/EvilMask/Lang.cs:1204`），占位符用了 `[0]` 而不是 .NET 的 `{0}`，`string.Format` 不会代入方法名——报告中该方法名只会原样打印（推定，语言 XML 若把该文本改写为 `{0}` 则可正常代入）。
- 移植实现与覆盖取舍另见 [CALLSHARP 移植说明](../../change/commands.md#callsharp)。
