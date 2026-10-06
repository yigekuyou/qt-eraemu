# SET

- **类别**：命令（Emuera 枚举成员 `Runtime/Script/Statements/BuiltInFunctionCode.cs:15`；**不是**可在 ERB 中书写的关键字，而是赋值语句的引擎内表示。两套中文文档都没有以「SET」为名的命令小节，但赋值语句语法在 ecd 中有记载）
- **签名**（ERB 实际写法；解析器把它们统一包装成 `SET` 指令）：
  - `<变量> = <数值表达式 or 字符串表达式>`
  - `<变量> = <值1>, <值2>, …`（从该索引起连续写入多个元素，只对 `=` 有效；省略索引时从 0 开始）
  - `<变量> += <表达式>` / `-= <表达式>` / `*= <表达式>` / `/= <表达式>` / `%= <表达式>`
  - `<字符串变量> '= <字符串表达式>`
  - `++<变量>` / `--<变量>`（前置：行的第一个字符就是 `+`/`-`，走解析器的前置分支）
  - `<变量>++` / `<变量>--`（后置：`ReadAssignmentOperator` 把裸 `++`/`--` 也认作自增/自减运算符，右值必须为空）
  - `<变量> = <条件式> ? <为真时的值> # <为假时的值>`（三元运算符，见 ecd/Operator.md）
  - 也允许把 `=` 写成 `==`（解析器给出警告后当作 `=`）
- **文档来源**：`ecd/docs/translation/Command.md` 无 SET 小节；语法见 `ecd/docs/translation/ERB_Statements.md`「赋值语句」（第 127~162 行，含复合赋值与 `'=`）与 `ecd/docs/translation/Operator.md`「三元运算符」「赋值运算符」（第 164~195 行）。`Era-Chinese-Documentation/docs/ERB_Statements.md` 只有目录占位、无正文；`zh/Operator.md`「赋值运算符」章节也是空标题，仅在优先级表里列出符号。语义以源码为准（赋值语句虽两套文档都有记载，但「SET」这个名字与 `FunctionCode.SET` 枚举只存在于源码）

## 语义

赋值语句：把右侧的值写进左侧变量（数组元素以 `变量:索引` 形式指定）。左值必须是可写的变量项（不能是常量、不能是字面量、不能是逗号分隔的多个项），左右类型必须一致：整数变量只接受整数表达式，字符串变量只接受字符串表达式（`'=` 是字符串专用的复合赋值运算符，用字符串表达式给字符串变量赋值，见 ecd）。

复合赋值 `+= -= *= /= %=` 有两种实现路径：右侧是常量且运算符是 `+`/`-` 时走「常量加法捷径」（`IsConst=true, AddConst=true`），执行期用 `PlusValue` 就地累加；其余情况在**解析期**展开为 `左值 op 右值` 的二元表达式，再整体赋值。`*=` 用于字符串变量时是「字符串 × 整数 = 重复」的语义（`Runtime/Script/Statements/Expression/OperatorMethod.cs` 的 `MultStrInt`，ecd/ERB_Expressions.md 第 159 行有记载）。

行的第一个字符是 `+`/`-` 时，按「前置自增/自减」解析：`++A` 等于 `A += 1`（`IsConst=true, ConstInt=±1, AddConst=true`）。

因为是普通赋值行，`SET` 这个标识符本身**不能**写进 ERB：`funcDic` 里没有 `"SET"` 键（`Runtime/Script/Statements/FunctionIdentifier.cs:82` 只把对象赋给了静态字段 `setFunc`，全仓只有 `Runtime/Script/Parser/LogicalLineParser.cs:413/588` 用它构造赋值行），写 `SET A = 1` 时 `SET` 不是已注册命令，整行落到一般赋值行分支，左值集合里会多出一个 `SET` 标识符，解析左值时失败（报「无法解释的标识符」类错误），所以赋值语句不能写 `SET` 关键字。

## 用法

### <变量> = <值> / 复合赋值 / '=

- `<变量>`：可写变量或带索引的数组元素（如 `A`、`CFLAG:0:5`）。
- 右侧：与变量类型一致的表达式；`=` 的右侧也可以是用逗号分隔的多个值（按 `SpSetArrayArgument` 连续写入，形如 `ARR:2 = 1, 2, 3` 会写 `ARR:2`、`ARR:3`、`ARR:4`；只对 `=` 有效）。

```erb
A = 10
S = "文本"
A = A + 1              ; 先算右边再存回左边
CFLAG:0:5 = 100        ; 给数组元素赋值
A += 1                 ; 常量捷径：PlusValue
A *= 2                 ; 解析期展开为 A = A * 2
S '= "文本" + TOSTR(A) ; 字符串专用复合赋值
FLAG = 1, 2, 3         ; 多值赋值（数组）
```

### ++<变量> / --<变量>（前置与后置）

- 前置：整行以 `+`/`-` 开头且操作符是 `++`/`--`（`Runtime/Script/Parser/LogicalLineParser.cs:393~414`），否则报「以 + 开始但不是自增」等错误。
- 后置：`A++` 走一般赋值行，`ReadAssignmentOperator` 读到裸 `++` → `OperatorCode.Increment`（`Runtime/Script/Parser/LexicalAnalyzer.cs:653~663`），右值必须为空（`Runtime/Script/Statements/ArgumentBuilder.cs:1041~1054`）。
- 两者都展开成「常量 +1/-1 的就地累加」（`AddConst=true`）。

```erb
++A   ; A = A + 1（前置）
A++   ; A = A + 1（后置，效果相同）
--A   ; A = A - 1
A--   ; A = A - 1
```

### <变量> = <条件式> ? <真值> # <假值>

三元运算符由表达式求值负责（`Runtime/Script/Statements/Expression/ExpressionParser.cs:659~678`），SET 只负责把结果写回左值。

```erb
MONEY = MONEY > 100 ? MONEY - 100 # MONEY
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:82`（`setFunc = new FunctionIdentifier("SET", FunctionCode.SET, new SET_Instruction())`，仅存于静态属性 `SETFunction`，`Runtime/Script/Statements/FunctionIdentifier.cs:497`，**未**注册进 `funcDic`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:15`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:603`（`SET_Instruction`，构造在第 605~609 行设 `ArgBuilder=SP_SET`、`flag=METHOD_SAFE`，执行在第 610~658 行）
- 参数构造：`Runtime/Script/Statements/ArgumentBuilder.cs:999`（`SP_SET_ArgumentBuilder`，第 999~1230 行）
- 解析入口：`Runtime/Script/Parser/LogicalLineParser.cs:393~414`（前置 `++`/`--`）与第 556~588 行（一般赋值行）

```text
解析期 LogicalLineParser.ParseLine（行 383~597）:
    若行首是 '+' 或 '-'（第 393~414 行）:
        按操作符解析；操作符不是 ++ / -- → InvalidLine（"以+开始但不是自增"等）
        return InstructionLine(SETFunction, Increment/Decrement, 变量部分, null)
    否则:
        先按命令名解析（第 416~548 行）；"SET" 不在 funcDic，故正常赋值行会落到下面
        回退为赋值行（第 556~588 行）:
            wc1 = 跳过到第一个运算符为止的字词（LexEndWith.Operator）   # 左值文本
            assignOP = ReadAssignmentOperator()                      # = += -= *= /= %= '= 等
                # 裸 "++"/"--" 也在这里被识别（LexicalAnalyzer.cs:653~663），
                # 所以 "A++"/"A--" 同样是赋值行，其右值文本为空
            若 assignOP == "==" → 警告「用 == 代替 =」后改成 "="（第 581~587 行）
            return InstructionLine(SETFunction, assignOP, wc1, 右值 charStream)

参数构造 SP_SET_ArgumentBuilder.CreateArgument（ArgumentBuilder.cs:999~1230）:
    destTerms = ReduceArguments(左值)（第 1003~1004 行）
    左值 为空 / 非单项 / 不是 VariableTerm / 是常量 → assignwarn 并 return null（第 1006~1025 行）
    varTerm.Restructure(exm)
    st = line.PopArgumentPrimitive()          # 右值文本；null 时视为空串（第 1027~1029 行）
    op = line.AssignOperator
    若 varTerm.IsInteger（第 1032~1137 行）:
        op == AssignmentStr("'=") → 警告「此运算符不能用于整数」（第 1034~1038 行）
        op 是 ++ / --（前置）:
            右值非空 → 警告并失败（第 1041~1054 行）
            否则 SpSetArgument{IsConst=true, ConstInt=±1, AddConst=true}（第 1055~1061 行）
        否则解析右值为表达式列表:
            多项:
                op != "=" → 警告「不能使用多个值」（第 1071~1077 行）
                每项必须是整数，否则警告「不能把字符串赋给整数」
                全为常量时记录常量数组 → SpSetArrayArgument（第 1078~1102 行）
            单项:
                非整数 → 警告「不能把字符串赋给整数」（第 1104~1108 行）
                op == "=" 且为常量 → SpSetArgument{IsConst, ConstInt}（第 1110~1119 行）
                op 是 + / - 且为常量 → SpSetArgument{IsConst, ConstInt=±值, AddConst=true}（第 1121~1132 行）
                否则 src = ReduceBinaryTerm(op, varTerm, src)（第 1134 行）
                   # 复合赋值在此展开为二元表达式后整体赋值
    否则 varTerm.IsString（第 1138~1229 行）:
        op == "=":
            配置「禁止字符串赋值」→ 警告并失败（第 1140~1146 行）
            右值按 FORM 字符串解析（第 1149~1151 行）
            常量为字符串字面量 → SpSetArgument{IsConst, ConstStr}（第 1152~1158 行）
        op ∈ {*=, +=, '=}（第 1161 行起）:
            单项：必须是字符串 → SpSetArgument（第 1171~1189 行）
            多项：只有 '= 允许 → SpSetArrayArgument（第 1190~1214 行）
            否则 src = ReduceBinaryTerm(op, varTerm, src)（第 1222~1224 行）
                # + 为连接；* 为「字符串 × 整数 = 重复」（OperatorMethod.cs:250 MultStrInt）
        其他运算符 → 警告「无效的赋值运算符」（第 1226 行）

执行期 SET_Instruction.DoInstruction（Instraction.Child.cs:610~658）:
    若 func.Argument 是 SpSetArrayArgument（多值路径，第 612~643 行）:
        变量是整数型:
            IsConst → 常量数组直接赋值；否则逐个 GetIntValue 后赋值
        否则:
            IsConst → 常量字符串数组直接赋值；否则逐个 GetStrValue 后赋值
        return
    spsetarg = (SpSetArgument)func.Argument
    若变量是整数型（第 645~652 行）:
        src = IsConst ? ConstInt : Term.GetIntValue(exm)
        AddConst ? VariableDest.ChangeValue(src, exm)   # ≡ PlusValue 就地累加
                 : VariableDest.SetValue(src, exm)
    否则（第 653~657 行）:
        src = IsConst ? ConstStr : Term.GetStrValue(exm)
        VariableDest.SetValue(src, exm)

VariableTerm.SetValue(long/string, exm)（VariableTerm.cs:82 / 98）:
    非全常量索引时先对 arguments 逐个求值填入 transporter
    Identifier.SetValue(value, transporter)
    越界（IndexOutOfRangeException 等）→ Identifier.CheckElement() 抛 CodeEE
VariableTerm.ChangeValue（VariableTerm.cs:154~169）:
    Identifier.PlusValue(value, transporter)  # 原地累加并返回新值
```

## 备注

- **两套中文文档都没有「SET 命令」条目**，只有赋值语句语法。ecd 的表里「赋值语句|把值装进变量|`A = 10`」正是本指令的文档形态；因此本文件把 `SET` 记为「赋值语句的引擎内表示」而非可书写关键字。
- `flag = METHOD_SAFE`（不含 `EXTENDED`）：赋值语句在 `#FUNCTION` 式中函数里也可用；它不是 EM/EE 私有扩展，而是原本就存在的核心语句（枚举成员本身在 `Runtime/Script/Statements/BuiltInFunctionCode.cs:15`，注释「数値代入文 or 文字列代入文」）。**未改造的原版 Emuera 1.824（仓库根 `Emuera/`）里同样如此**：`Runtime/Script/Statements/FunctionIdentifier.cs:83` 有同一个 `setFunc = new FunctionIdentifier("SET", …)` 写法，`Runtime/Script/Statements/BuiltInFunctionCode.cs:18` 有同名枚举成员，`Runtime/Script/Statements/Instraction.Child.cs:414` 有同名指令类。
- 文档未记载的解析细节：`==` 被接受为赋值号但会有警告（`Runtime/Script/Parser/LogicalLineParser.cs:581~587`）；`++A`/`--A`（行首）与 `A++`/`A--`（裸双符号被 `ReadAssignmentOperator` 识别）两种写法都能成立且**不允许**再带右值，两者都按 `AddConst` 常量捷径执行（与表达式内部的 `A++` 求值语义不同：作为赋值行时整行只做一次就地累加）。
- `AddConst` 常量捷径只对 `+=` / `-=` / `++` / `--` 生效；`*= /= %=` 一律走「解析期展开为二元表达式」路径，两者对 `RESULT`/副作用的影响相同，只是求值次数不同（捷径只求值一次）。
- 越界赋值由 `VariableTerm.SetValue` 捕获 `IndexOutOfRangeException` / `ArgumentOutOfRangeException` / `OverflowException` 后调用 `Identifier.CheckElement`，转成 CodeEE 报错（`Runtime/Script/Statements/Variable/VariableTerm.cs:91~96`）。
