# VARS

- **类别**：EE 扩展命令（Emuera 枚举成员 `Runtime/Script/Statements/BuiltInFunctionCode.cs:394`，位于 `#region Emuera.NET`（第 392 行）；两套中文文档均未收录，语义据源码）
- **签名**（据 `LogicalLineParser` 的专用解析分支推定）：
  - `VARS <变量名>`：声明一维长度 1 的字符串私有变量，初值空（null）
  - `VARS <变量名> = "<字符串>"`：声明并写初值（长度仍为 1；**初值必须用双引号包围**）
  - `VARS <变量名>, <长度1>{, <长度2>{, <长度3>}}`：声明 1/2/3 维字符串数组（**不写初值**，全为 null/空串）
- **实现状态**：**已实现，但默认关闭**，与 `VARI` 完全同级：仅当 `setting.json` 的 `UseScopedVariableInstruction` 为 `true` 时注册（`Runtime/Script/Statements/FunctionIdentifier.cs:437~440`）；关闭时在解析期报「`VARS`命令は現在の設定では使用できません」（`Runtime/Script/Data/IdentifierDictionary.cs:661~665`、`Runtime/Utils/EvilMask/Lang.cs:1200`）。
- **与 `VARSIZE`/`VARSET` 的关系**：**无关**。`VARSIZE`/`VARSET` 是另两个枚举成员（`Runtime/Script/Statements/BuiltInFunctionCode.cs:122`/`239`，注册在 `Runtime/Script/Statements/FunctionIdentifier.cs:399`/`317`），语义为「取数组长度」「批量赋值」；ecd `ERB_Commands.md` 表中出现的 `VARSIZE`、`VARSET`、`CVARSET` 均与 `VARS` 不同名——`grep -rn "^#* *VARS\b"` 在两套提取文档中零命中，所以 ecd 中的「VARS」只是 `VARSIZE`/`VARSET` 的子串，不存在「VARS 是 VARSIZE 笔误」的对应关系。`VARS` 取自内置变量代码 `VariableCode.VARS`（`Runtime/Script/Statements/Variable/VariableToken.cs:2278`）。
- **文档来源**：无中文文档收录。依据源码：`Runtime/Script/Statements/FunctionIdentifier.cs:437~440`、`Runtime/Script/Statements/Instraction.Child.cs:58`、`Runtime/Script/Parser/LogicalLineParser.cs:494~529`、`Runtime/Script/Statements/Argument.cs:645`、`Runtime/Script/Statements/Variable/VariableToken.cs:2275`、`Runtime/Script/Statements/Variable/VariableData.cs:443`

## 语义

`VARS` 是 `VARI` 的字符串版：在**当前函数**里声明一个字符串型的函数私有变量（长度 1 的标量，或 1~3 维数组），可顺带写入初值。作用域、生命周期、执行期流程都与 `VARI` 相同：

- 作用域为声明所在的函数标签（`func.ParentLabelLine.AddPrivateVariable`），同名已存在时第二次声明被忽略（`Runtime/Script/Statements/LogicalLine.cs:288~297`）。
- 变量是**动态私有变量**（`Static = false`）：每次进入函数时 `ScopeIn` 重建数组（字符串默认 null，读出来按空串处理），函数返回时 `ScopeOut` 归还外层，不跨调用保留、不进存档。
- 执行到该行时先 `ScopeIn()`，再判断 `GetLength(0) == 1`：是则把初值写进元素 `[0]`，否则（数组声明）什么都不做。

行为边界（读码推定，文档未记载）：

- **初值必须带引号**：解析器用 `right.IndexOf('"')` / `right.LastIndexOf('"')` 取两个引号之间的内容（`Runtime/Script/Parser/LogicalLineParser.cs:511~513`）。若写成 `VARS S = abc`（无引号），得到 `right[0..-1]` 这样的空区间，会抛 `ArgumentOutOfRangeException`（未受控的 .NET 异常；装载期由 `Runtime/Script/Loader/ErbLoader.cs:115` 的调用点外层兜底 catch 捕获，`Runtime/Script/Loader/ErbLoader.cs:124~129` 打印「予期せぬエラー」并中止装载）。
- 取的是**第一个引号到最后一个引号之间的全部内容**，中间可以再出现引号（不做转义处理）。
- 数组形式（长度 >1 或 2/3 维）不赋初值。
- 二维以上且第一维长度恰为 1 的写法（如 `VARS S, 1, 5`）会走进「长度==1」分支，把 1 元素索引数组交给 2/3 维变量 `SetValue`，访问 `arguments[1]` 时抛 `IndexOutOfRangeException`。
- 维度数字必须是整数**字面量**（`int.Parse`，失败抛 `FormatException`）；变量名是裸标识符，不能带索引。
- 与 `SET` 路径不同，`VARS S = "..."` **不**走 FORM 字符串解析，也不做类型检查——它就是「声明 + 单元素赋值」。

## 用法

### VARS <变量名> / VARS <变量名> = "<字符串>"

```erb
@TEST
  VARS NAME               ; 声明标量，初值空串
  VARS TITLE = "你好"      ; 声明并写初值（必须带引号）
  NAME = "改一下"          ; 之后按普通私有变量使用
  PRINTFORML [%TITLE%] %NAME%
```

### VARS <变量名>, <长度1>{, <长度2>{, <长度3>}}

```erb
@TEST
  VARS SLOT, 5            ; 一维长度 5（初值全为空串；不写初值）
  VARS GRID, 2, 3         ; 二维
  SLOT:0 = "剑"
  GRID:1:2 = "宝箱"
```

### 开启方式

与 `VARI` 共用开关：`setting.json` 的 `"UseScopedVariableInstruction": true`，或配置对话框「VAR系命令を利用可能にする」。

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:440`（`addFunction(FunctionCode.VARS, new VARS_Instruction())`，处在 `if (JSONConfig.Data.UseScopedVariableInstruction)` 与 `#region Emuera.NET`（第 436 行）之内）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:394`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:58`（`VARS_Instruction`，`#region Emuera.NET VAR命令`，执行第 60~75 行，`CreateArgument` 第 77~80 行返回 null）
- 解析（参数在这里生成）：`Runtime/Script/Parser/LogicalLineParser.cs:494~529`（VARS 分支；第 423 行是 VARI/VARS 的共同入口）；参数对象 `Runtime/Script/Statements/Argument.cs:645`（`StrAsignArgument`）
- 变量实体：`Runtime/Script/Statements/Variable/VariableData.cs:443`（`CreatePrivateVariable` 动态私有变量分支 → `PrivateStr1DVariableToken`，`Runtime/Script/Statements/Variable/VariableToken.cs:2275`）

```text
解析期 LogicalLineParser.ParseLine（LogicalLineParser.cs:424~530）:
    标识符为 VARI 或 VARS（第 423 行）→ 建 InstructionLine 并绑定 ParentLabelLine
    text = 去掉 ';' 注释的原样文本；在第一个 '=' 处切成 left / right
    leftSplit = left.Split(',')；varName = leftSplit[0].Trim()；lengths = [1]
    —— VARS 分支（第 494~529 行）:
        value = default（null）
        若 leftSplit.Length > 1:                      # 数组声明
            lengths = leftSplit[1..] 逐个 int.Parse     # 失败 → FormatException
        否则若 right 非空白:                            # 初值
            literalStart = right.IndexOf('"')
            literalEnd   = right.LastIndexOf('"')
            value = right[(literalStart + 1)..literalEnd]     # 无引号时区间非法 → ArgumentOutOfRangeException
        varData = UserDefinedVariableData{ Name=varName, Static=false,
                                           Lengths=lengths, Dimension=lengths.Count, TypeIsStr=true }
        parentLine.AddPrivateVariable(varData)
        line.Argument = StrAsignArgument(varName, lengths, value)   # 注意：不检查 value 是否为空
        return line

执行期 VARS_Instruction.DoInstruction（Instraction.Child.cs:60~75）:
    arg = (StrAsignArgument)func.Argument
    varName = arg.ConstStr
    privateVar = func.ParentLabelLine.GetPrivateVariable(varName)
    privateVar.ScopeIn()                    # 动态私有变量：重建数组（字符串默认 null）
    若 privateVar.GetLength(0) == 1:
        privateVar.SetValue(arg.Value, [0])  # 写入解析期取出的字面量内容
    否则:                                    # 数组/多维：什么都不做（空 else 块）
        （空）

PrivateStr1DVariableToken（VariableToken.cs:2275~2343）:
    构造: 基类 VariableCode.VARS；IsStatic = false
    ScopeIn  (2321~2330): 旧数组入栈 → array = new string[sizes[0]]
    ScopeOut (2332~2342): 栈非空则弹回，否则置 null
    SetValue(string value, long[] arguments): array[arguments[0]] = value
```

## 备注

- **Emuera.NET 系私有扩展**，与 `VARI` 同族同开关；两套中文文档、EmueraEE readme/changelog、私家改造版 readme（Shift-JIS 转码后 grep）均无记载。**未改造的原版 Emuera 1.824（仓库根 `Emuera/`）里没有 `FunctionCode.VARS` 命令**（`grep -rn "FunctionCode.VARS" Emuera/` 零命中；`VARS` 在原版只作为变量代码出现，如 `Emuera/GameData/Variable/VariableCode.cs:277` 的 `VARS = 0xFE | __STRING__ | __ARRAY_1D__ | __EXTENDED__`），故本命令也是 emuera.em 新增。
- 名字辨析：ecd/ERB_Commands.md 的变量操作表里有 `VARSIZE`、`VARSET`、`CVARSET`，**没有** `VARS` 命令；`VARS` 只作为前两者的子串出现。本命令与它们没有别名/旧名关系（详见 `commands/VARI.md` 备注的 4 条证据）。
- 与 `SET` 写字符串变量的差异：`S = "x"` 走 `SP_SET` 构造器（支持 `'=`、`+=` 连接、`*=` 重复、多值数组赋值、FORM 解析与类型检查），而 `VARS S = "x"` 只是声明+单元素赋值；`VARS` 行里**不能**写复合赋值运算符，`=` 右侧也只按「首尾引号之间」取文本。
- 字符串初值取的是「第一个引号之后、最后一个引号之前」的子串，因此 `VARS S = "a" + "b"` 得到的是 `a" + "b` 这样的字面内容——不是表达式求值结果（与 `SET` 完全不同）。
- 与之配对的整数版见 `commands/VARI.md`。
