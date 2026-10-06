# GETVAR

- **类别**：式中函数（EM 私家版扩展，`#region EM_私家版_追加関数`）
- **签名**：int GETVAR(str 变量名表达式)
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:79-90`「◆ int GETVAR str / str GETVARS str / int(1) SETVAR str, value」（含示例与结果）；两套中文文档（`ecd/`、`_extracted/zh/`）未收录

## 语义

把参数字符串当作**变量名表达式**求值，返回其（整数值）。是「字符串 → 变量访问」的动态取值入口，与 `SETVAR`（赋值，`Emuera.EM_readme.txt:81`）、`VARSETEX`（批量赋值，`:91`）配套。

- 参数字符串在**运行期**被词法/语法分析器解析为单个表达式词（`LexicalAnalyzer.Analyse` + `ExpressionParser.ReduceExpressionTerm`，`Runtime/Script/Statements/Function/Creator.Method.cs:277-278`），因此可以写带下标的变量名：`"FOO:1"`、`"CFLAG:TARGET:3"`（下标里还能再嵌表达式，取决于解析器对词项的支持，推定）。
- 解析结果必须是**变量**且必须是**整数型**：
  - 结果不是变量（如 `"1+2"`、`"RAND:1"` 等非变量词）→ 抛 `CodeEE`（`IsNotVar`）；
  - 是字符串型变量 → 抛 `CodeEE`（`IsNotInt`）；字符串取值请用 `GETVARS`。
- 返回值即该变量的当前值（含下标处的值），不复制、不修改。
- `CanRestructure = false`：即使参数是常量字符串也在运行期求值（因为结果是变量值，装载期可能尚未有意义）；也因此每次调用都会重新解析字符串（性能上有开销，推定）。
- 与 `EXISTVAR`（查变量是否存在与性质）互补：可以用 `EXISTVAR` 先探测，再 `GETVAR` 取值。

## 用法

### int GETVAR(str 变量名表达式)
- 变量名表达式：字符串表达式，内容是一个整数变量词（可带下标）。
- 返回值：该变量的整数值。
- 报错：内容不是变量（`IsNotVar`）、或是字符串变量（`IsNotInt`）。
```erb
#DIM FOO = 10, 10

SETVAR "FOO:1", 5                       ; readme 示例
PRINTFORML {FOO} {FOO:1} {GETVAR("FOO:1")}   ;→ 10 5 5

PRINTFORML {GETVAR("FOO")}              ;→ 10（下标 0）
PRINTFORML {GETVAR("DAY:0")}            ;→ 1（系统变量也能取）
; PRINTFORML {GETVAR("1+2")}            ; 报错：不是变量
; PRINTFORML {GETVAR("NAME")}           ; 报错：NAME 是字符串变量 → 应改用 GETVARS
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:237`（`["GETVAR"] = new GetVarMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:265`（`GetVarMethod`）；姊妹版 `GetVarsMethod` 于 `:294`
- 赋值对照：`SetVarMethod`（`Runtime/Script/Statements/Function/Creator.Method.cs:503`，注册 `Runtime/Script/Statements/Function/Creator.cs:239`）

```text
构造（Creator.Method.cs:267-272）:
    返回类型 = long
    argumentTypeArray = [typeof(string)]      ; 恰好 1 个字符串参数
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:274-292）:
    name = args[0].GetStrValue(exm)
    wc   = LexicalAnalyzer.Analyse(new CharStream(name), LexEndWith.EoL, LexAnalyzeFlag.None)
    term = ExpressionParser.ReduceExpressionTerm(wc, TermEndWith.EoL)   ; 当作单个词项解析
    if term is VariableTerm var:
        if var.Identifier == null:   throw CodeEE(IsNotVar, name)
        if !var.IsInteger:           throw CodeEE(IsNotInt, name)
        return var.GetIntValue(exm)                     ; 取当前值（含下标）
    else:
        throw CodeEE(IsNotVar, name)
```

## 备注

- readme 与源码一致（含示例结果 `10 5 5`），无冲突。
- 与 `VARSIZE("名字")`（按名字取数组大小）的区别：本函数取值，`VARSIZE` 取尺寸；`GETVAR` 不接受非变量词项。
- 因为走解析器，参数字符串里写**函数调用**（如 `"RAND:1"`）不会被当作表达式求值——`ReduceExpressionTerm` 取到的是词项；只有「变量词（可带下标）」能通过 `term is VariableTerm` 判定（推定：下标里的表达式是否展开取决于词项解析细节，本函数不强求）。
- 每次调用都做一次字符串解析，热的循环里应改用普通变量访问。
- 本仓库移植版（`src/eraengine/`）未实现本函数。
