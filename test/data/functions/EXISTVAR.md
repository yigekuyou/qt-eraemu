# EXISTVAR

- **类别**：式中函数（EM 私家版扩展，`#region EM_私家版_追加関数`）
- **签名**：int EXISTVAR(str 变量名)
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:32-62`「◆ int EXISTVAR str」（含位含义与示例）；两套中文文档（`ecd/`、`_extracted/zh/`）未收录

## 语义

按名字查询变量是否已定义，并按**位标志**返回其性质；未定义返回 `0`。

| 返回值 | readme 的表述 | 含义（源码 `Runtime/Script/Statements/Function/Creator.Method.cs:337-341`） |
|---|---|---|
| `1` | setbit 1 | 整数型 |
| `2` | setbit 2 | 字符串型 |
| `4` | setbit 3 | 常量（`#DIM CONST` 等声明为不可变更的变量） |
| `8` | setbit 4 | 二维数组 |
| `16` | setbit 5 | 三维数组 |

- 多个位可同时成立（例如整数 + 二维数组 = `1 | 8 = 9`）；一维数组不设位（既不是二维也不是三维）。
- 名字解析走 `IdentifierDictionary.GetVariableToken(name, null, true)`（`Runtime/Script/Statements/Function/Creator.Method.cs:333`），因此**只认广域变量名**：系统变量、写进 ERH 的用户定义变量；ERB 函数内的私有 `#DIM` 局部变量不可见（推定：第二个参数传 `null` 表示无局部命名空间）。
- 名字区分大小写的规则由变量字典的比较器决定（Emuera 默认忽略大小写）。
- `CanRestructure = true`：常量参数会被折叠，即在**解析期**就把结果算好（`Runtime/Script/Statements/Function/FunctionMethodTerm.cs:33-47`）。
- 与 `ISDEFINED`（`Emuera.EM_readme.txt:30`，判断宏是否存在）区别：本函数针对**变量**。

## 用法

### int EXISTVAR(str 变量名)
- 变量名：字符串表达式（如 `"FOO"`、`"FOO:1"`——带下标时仍按变量本体判定，因为下标是求值时才用）。
- 返回值：性质位或 `0`。
```erb
; 以下声明写在 ERH（头文件）里 —— 本函数只查得到广域变量与系统变量
#DEFINE VAR_IS_NUM 1
#DEFINE VAR_IS_STRING 2
#DEFINE VAR_IS_CONST 1p2
#DEFINE VAR_IS_2DARRAY 1p3
#DEFINE VAR_IS_3DARRAY 1p4

#DIM FOO, 10, 10          ;二维整数数组
#DIMS BAR = "x"
#DIM CONST FOO2 = 5         ;一维整数常量（广域变量用 CONST 合法）

PRINTFORML {EXISTVAR("FOO")}      ;→ 9（1 | 8：整数 + 二维数组）
PRINTFORML {EXISTVAR("BAR")}      ;→ 2（字符串、一维）
PRINTFORML {EXISTVAR("FOO2")}     ;→ 5（1 | 4：整数 + 常量）
PRINTFORML {EXISTVAR("COUNT")}    ;→ 1（系统变量 COUNT 是整数一维数组）
PRINTFORML {EXISTVAR("NOPE")}     ;→ 0（未定义）

IF EXISTVAR("FOO") == (VAR_IS_NUM | VAR_IS_2DARRAY)
	PRINTL TRUE
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:223`（`["EXISTVAR"] = new ExistVarMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:322`（`ExistVarMethod`）
- 变量查找：`Runtime/Script/Data/IdentifierDictionary.cs`（`GetVariableToken`）

```text
构造（Creator.Method.cs:324-329）:
    返回类型 = long
    argumentTypeArray = [typeof(string)]      ; 恰好 1 个字符串参数
    CanRestructure = true

GetIntValue(exm, args)（Creator.Method.cs:331-345）:
    token = GlobalStatic.IdentifierDictionary.GetVariableToken(args[0] 的字符串值, null, true)
    if token == null: return 0
    res = 0
    if token.IsInteger: res |= 1
    if token.IsString:  res |= 2
    if token.IsConst:   res |= 4
    if token.IsArray2D: res |= 8         ; 注意是"值是 8"，不是"第 8 位"
    if token.IsArray3D: res |= 16
    return res
```

## 备注

- readme 与源码**一致**：正文用「setbit 1..5」（从 1 起数位，即值 1/2/4/8/16）描述，示例中的宏写作 `VAR_IS_CONST 1p2`（= 4）、`VAR_IS_2DARRAY 1p3`（= 8）、`VAR_IS_3DARRAY 1p4`（= 16）——`1pN` 是「1 左移 N 位」，与源码的位值一一对应。阅读 readme 时勿把「setbit 3」误读成「值 3」。
- 一维数组没有对应位：`#DIMS BAR`（字符串一维）返回 2，`#DIM` 标量与一维数组同样是 `1`，无法靠本函数区分标量/一维数组。
- `#DIM` 在函数内部声明的是该函数的私有变量（局部作用域），本函数按名字在全局表里查找，因此测不到（推定；写进 ERH 的 `#DIM` 才是广域变量，见 `Runtime/Script/Loader/ErhLoader.cs:290`/`Runtime/Script/Data/IdentifierDictionary.cs:431-433`）。
