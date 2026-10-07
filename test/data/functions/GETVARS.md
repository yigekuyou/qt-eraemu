# GETVARS

- **类别**：式中函数（EM 私家版扩展，`#region EM_私家版_追加関数`）
- **签名**：str GETVARS(str 变量名表达式)
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:79-90`「◆ int GETVAR str / str GETVARS str / int(1) SETVAR str, value」；两套中文文档（`ecd/`、`_extracted/zh/`）未收录

## 语义

`GETVAR` 的字符串版：把参数字符串当作**字符串型变量名表达式**求值并返回其值。

- 参数字符串在运行期被解析为单个表达式词（`Runtime/Script/Statements/Function/Creator.Method.cs:305-306`），可写带下标的字符串变量名，如 `"NAME:TARGET:1"`、`"CSTR:MASTER:0"`。
- 结果的类型必须匹配：
  - 不是变量（如 `"ABC"` 这种裸标识符未定义、或 `"1+2"`）→ 抛 `CodeEE`（`IsNotVar`）；
  - 是**数值型**变量 → 抛 `CodeEE`（`IsNotStr`）；数值取值请用 `GETVAR`。
- 返回值是该字符串变量的当前值（可为空串）。
- `CanRestructure = false`（运行期求值，每次重新解析字符串）。配套的赋值入口是 `SETVAR`（数值）与 `VARSETEX`（批量），字符串型变量没有专用的 `SETVARS` 名称函数——`SETVAR` 的值参数是 `Any`，可以给字符串变量赋字符串（见 `SetVarMethod`，`Runtime/Script/Statements/Function/Creator.Method.cs:503-511`；本批条目只覆盖 `GETVARS`，`SETVAR` 的实现细节请另见该函数文档）。

## 用法

### str GETVARS(str 变量名表达式)
- 变量名表达式：字符串表达式，内容是一个字符串变量词（可带下标）。
- 返回值：该变量的字符串值。
- 报错：不是变量（`IsNotVar`）、或是数值变量（`IsNotStr`）。
```erb
#DIMS NAME = "甲"

PRINTFORML [{GETVARS("NAME")}]        ;→ [甲]（下标 0）
PRINTFORML [{GETVARS("NAME:0")}]      ;→ [甲]
SETVAR "NAME", "乙"                   ; 用 SETVAR 给字符串变量赋值（值参数为 Any）
PRINTFORML [{GETVARS("NAME")}]        ;→ [乙]
PRINTFORML [{GETVARS("CALLNAME:MASTER")}]   ; 系统字符串变量也可（角色变量按下标）
; PRINTFORML [{GETVARS("DAY")}]       ; 报错：DAY 是数值变量 → 应改用 GETVAR
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:238`（`["GETVARS"] = new GetVarsMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:294`（`GetVarsMethod`）
- 数值版对照：`GetVarMethod`（`Runtime/Script/Statements/Function/Creator.Method.cs:265`）

```text
构造（Creator.Method.cs:296-301）:
    返回类型 = typeof(string)
    argumentTypeArray = [typeof(string)]      ; 恰好 1 个字符串参数
    CanRestructure = false

GetStrValue(exm, args)（Creator.Method.cs:302-320）:
    name = args[0].GetStrValue(exm)
    wc   = LexicalAnalyzer.Analyse(new CharStream(name), LexEndWith.EoL, LexAnalyzeFlag.None)
    term = ExpressionParser.ReduceExpressionTerm(wc, TermEndWith.EoL)
    if term is VariableTerm var:
        if var.Identifier == null: throw CodeEE(IsNotVar, name)
        if !var.IsString:          throw CodeEE(IsNotStr, name)
        return var.GetStrValue(exm)              ; 取当前值（含下标）
    else:
        throw CodeEE(IsNotVar, name)
```

## 备注

- readme 只给了 `GETVAR` 的示例（数值），字符串版仅有签名行；本条的示例与错误行为由源码推出。
- 因为没有 `SETVARS`，字符串变量的「按名字赋值」要用 `SETVAR`（其第 2 参数为 `Any`，字符串值原样写入字符串变量；见 `SetVarMethod` 的参数表 `{ String, Any }`，`Runtime/Script/Statements/Function/Creator.Method.cs:508-510`）。
- 与 `GETVAR` 一样每次调用都要解析字符串，且**解析失败即报错**（不像 `MAP_GET` 那样返回空串吞掉错误）——脚本里要容错应先 `EXISTVAR`。
