# STRJOIN

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数
- **签名**：str STRJOIN(var array, str 分隔符 = ",")
- **签名**：str STRJOIN(var array, str 分隔符, int start = 0)
- **签名**：str STRJOIN(var array, str 分隔符, int start, int length)
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 未收录。语义依据源码与官方 wiki（exmeth「式中で使用できる関数」）补写

## 语义

把一维（或多维数组沿最后一维取的一段）数组变量的元素用分隔符连接成一个字符串并返回。第 1 参数必须是数组变量名（可带固定维下标，如 `LOCALS`、`CSTR:1` 的形式定位到某一维）。连接范围从 `start` 开始、共 `length` 个元素；`start` 默认 0，`length` 默认为"数组该维长度 - start"（即到末尾）。分隔符默认 `","`。数值数组会把各元素转成十进制字符串后连接。`length` 为负时报运行时错误；范围越界（`start`、`start + length` 超出该维大小）也报运行时错误。

也可以写成独立命令形式 `STRJOIN array(, 分隔符, start, length)`，结果存入 `RESULTS:0`。

## 用法

### str STRJOIN(var array, str 分隔符 = ",")

- `array`：数组变量（1~3 维；多维时用固定下标定位后沿最后一维连接）。
- `分隔符`：元素间的字符串，可省略，默认 `","`。

### str STRJOIN(var array, str 分隔符, int start = 0)

- `start`：起始元素下标，可省略，默认 0。

### str STRJOIN(var array, str 分隔符, int start, int length)

- `length`：连接的元素个数；省略时取到数组末尾。

```erb
LOCALS:0 = "a" ; LOCALS:1 = "b" ; LOCALS:2 = "c"
STR = STRJOIN(LOCALS)              ; STR = "a,b,c"
STR = STRJOIN(LOCALS, "-")         ; STR = "a-b-c"
STR = STRJOIN(LOCALS, "-", 1)      ; STR = "b-c"
STR = STRJOIN(LOCALS, "-", 0, 2)   ; STR = "a-b"
DAY = STRJOIN(DAYNAME, "、")        ; 数值数组：元素逐个转成字符串连接
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:137`（`["STRJOIN"] = new JoinMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4944`（`public sealed class JoinMethod`），实际连接在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:275`（`GetJoinedStr`）

```text
函数 STRJOIN(varTerm, delimiter = ",", start = 0, length):
    若未给 length:
        length = varTerm.GetLastLength() - start     ; 最后一维长度 - start
    若 length < 0:
        throw CodeEE("STRJOINの第4引数(length)が負の値になっています")
    varTerm.IsArrayRangeValid(start, start + length, "STRJOIN", 2, 3)
        ; 越界时 throw CodeEE（OoRInstructionArg：范围 0..该维大小）
    返回 VariableEvaluator.GetJoinedStr(fixedTerm, delimiter, start, length)

GetJoinedStr(p, delimiter, index1, length):
    sum = ""
    若 p 是字符串数组:
        1 维:  返回 string.Join(delimiter, 数组, index1, length)
        2 维:  对 i in 0..length-1: sum += 元素[p.Index1, index1+i] + (非末尾 ? delimiter : "")
        3 维:  同上，下标为 [p.Index1, p.Index2, index1+i]
    否则（数值数组）:
        各维同上，元素先 .ToString() 再用 delimiter 连接
    返回 sum
```

## 备注

- 本批中 `ecd` 与 `zh` 两套文档均未收录此函数，语义完全依据源码补写。
- EE 发行版有同名扩展 `STRJOIN1`（`source_index.md` 标注 ABSENT，本仓库未实现），注意区分：本条只覆盖标准 `STRJOIN`。
- 2/3 维数组的连接是"固定前维下标、沿最后一维取段"，第 1 参数形如 `ARY:1` 时 `1` 就是那个固定下标。
