# SETVAR

- **类别**：式中函数（EM 扩展 / 变量名字符串化操作）
- **签名**：`int SETVAR(str 变量名, <值>)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:79-89`（「◆ int(1) SETVAR str, value」）有记载。语义以源码为准。

## 语义

按**字符串形式**给出变量名并赋值，等价于把该字符串当作赋值语句左值来写。例如 `SETVAR("FOO:1", 5)` 与 `FOO:1 = 5` 等效。第 1 参数在运行时被重新词法分析、解析成一个表达式的项，解析结果必须是**单个变量项**（`VariableTerm`）；否则抛 `CodeEE`。

类型检查在运行时做，与变量的实际类型对照：

- 目标是字符串型变量而给的值不是字符串 → `CodeEE`（`"◯◯"が整数型ではありません`）
- 目标是整数型变量而给的值不是整数 → `CodeEE`（`"◯◯"が文字列型ではありません`）
- 解析结果不是变量、或变量是常量（`#DIM` 之外的 `#DEFINE` 常量、`IsConst` 标记）/无标识符 → `CodeEE`（`"◯◯"が変数ではありません`）

成功返回 `1`。因为变量名是字符串，可以拼接后动态决定操作哪个变量（这是它相对普通赋值的唯一价值）。

读取侧的对应函数是 `GETVAR`（整数）与 `GETVARS`（字符串），本仓库同样已注册（`Runtime/Script/Statements/Function/Creator.cs:237`、`:238`）。

## 用法

### int SETVAR(变量名, 值)
- 变量名：字符串表达式，内容须是可解析为变量（可带下标）的名字，如 `"FOO"`、`"FOO:1"`、`"CFLAG:MASTER:0"`。
- 值：数值或字符串表达式；类型须与目标变量一致（运行时检查）。
- 返回值：成功 `1`；失败抛 `CodeEE`。
```erb
; EM readme 的原始示例
#DIM FOO = 10,10

SETVAR "FOO:1", 5
PRINTFORML {FOO} {FOO:1} {GETVAR("FOO:1")}
; 结果：10 5 5
```

```erb
; 动态选择变量名（这是 SETVAR 存在的意义）
$DYN_NAME = "CFLAG"
FOR I, 0, 5
    SETVAR $DYN_NAME + ":MASTER:" + TOSTR(I), I * 10
NEXT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:239`（`["SETVAR"] = new SetVarMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:503`（`SetVarMethod`），内部复用 `LexicalAnalyzer.Analyse` + `ExpressionParser.ReduceExpressionTerm`（`Runtime/Script/Statements/Expression/ExpressionParser.cs:143`）与 `VariableTerm.SetValue`（`Runtime/Script/Statements/Variable/VariableTerm.cs:82`/`:98`）

```text
SetVarMethod:
    构造:
        ReturnType = long
        argumentTypeArrayEx = [ { ArgTypes = { String, Any } } ]     # 恰好 2 个参数；第 2 参数任意类型
        CanRestructure = false
    GetIntValue(exm, arguments):
        name = arguments[0] 的字符串值                    # 仅用于错误信息
        wc = LexicalAnalyzer.Analyse(new CharStream(arguments[0] 的字符串值), EoL, None)
        term = ExpressionParser.ReduceExpressionTerm(wc, TermEndWith.EoL)   # 运行时重新解析

        若 term 是 VariableTerm var:
            若 var.Identifier == null 或 var.Identifier.IsConst:
                抛 CodeEE(IsNotVar, name)             # 「"{0}"が変数ではありません」
            若 var.IsString:                          # 字符串型变量
                若 arguments[1] 的类型 != string:
                    抛 CodeEE(IsNotInt, name)         # 「"{0}"が整数型ではありません」（错误信息与直觉相反，见备注）
                var.SetValue(arguments[1] 的字符串值, exm)      # 含下标求值
            否则:                                     # 整数型变量
                若 arguments[1] 的类型 != long:
                    抛 CodeEE(IsNotStr, name)         # 「"{0}"が文字列型ではありません」
                var.SetValue(arguments[1] 的整数值, exm)
            返回 1
        否则:
            抛 CodeEE(IsNotVar, name)
```

## 备注

- 语义据源码；EM readme 的示例与源码行为一致（`SETVAR "FOO:1", 5` 后 `FOO:1` 与 `GETVAR("FOO:1")` 均为 5）。
- **错误信息原文与判断相反**：字符串变量被赋非字符串值时抛的是 `IsNotInt`（「"FOO"が整数型ではありません」），整数变量被赋字符串值时抛的是 `IsNotStr`（「"FOO"が文字列型ではありません」），见 `Runtime/Script/Statements/Function/Creator.Method.cs:526`/`:532` 与 `Runtime/Utils/EvilMask/Lang.cs:1144`/`:1145`。两条消息都是把「变量名」当成出错对象来叙述，措辞易误解，此处如实记录。
- **参数个数固定为 2**（`ArgTypes = { String, Any }`，无 `OmitStart`），因此 `SETVAR "FOO"` 这种省略值的写法会在解析期报「引数が足りません」，不会把变量清零——这与命令 `VARSET`（值可省略）不同。
- 只能写**单个变量项**：`SETVAR "FOO + 1", 0` 解析结果不是 `VariableTerm`，抛「が変数ではありません」；`SETVAR "FOO:1+1", 0` 同理（下标必须是项的一部分，不能是任意表达式——下标允许是变量，如 `"FOO:IDX"` 可以，因为下标本身作为项被求值）。
- 变量名大小写与普通引用一致，受 `Config.IgnoreCase` 影响（`ExpressionParser` 侧同名规则）。
- 与 `VARSETEX` 的区别：`SETVAR` 写单个元素，`VARSETEX` 写一段范围/整个数组。
