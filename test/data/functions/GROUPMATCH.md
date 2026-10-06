# GROUPMATCH

- **类别**：式中函数
- **签名**：int GROUPMATCH(`? key`, `? value1`, `? value2`, ...)
- **文档来源**：`ecd/Expression.md`「内置表达式内函数一览」（仅一行签名 `int GROUPMATCH(? key, ? value1, ? value2...)`，无详解小节）；`ecd/Command.md` 未收录；zh 套件未收录

## 语义

统计第 1 参数（key）与后续各参数相等的个数并返回。即：在 `value1, value2, ...` 中数一数有多少个等于 `key`，返回该计数（0～参数数-1）。

第 1 参数的类型决定比较方式：key 为整数时全部按整数比较，key 为字符串时全部按字符串比较。后续参数必须与 key 同类型（构造器以 `VariadicSameAsFirst` 约束）。

典型用途是代替多重 `||` 判断，例如 `GROUPMATCH(A, 1, 2, 3)` 等价于 `A == 1 || A == 2 || A == 3`，且当 A 等于其中多个值时会返回大于 1 的计数。

## 用法

### int GROUPMATCH(key, value1, value2, ...)
- key：基准值，整数或字符串。
- value1、value2、...：与 key 同类型的比较值，个数不限（至少要能区分类型；`VariadicSameAsFirst` 要求与第 1 参数同型）。
- 返回值：与 key 相等的后续参数个数。
```erb
X = 2
PRINTV GROUPMATCH(X, 1, 2, 3)      ; 输出 1（只有 2 相等）
PRINTV GROUPMATCH(X, 2, 2, 5)      ; 输出 2（两个 2 相等）
PRINTVL GROUPMATCH("b", "a", "b")  ; 输出 1（字符串比较）
IF GROUPMATCH(TARGET, 0, 1, 2)
	PRINTL TARGET 是 0、1 或 2 之一
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:92`（`["GROUPMATCH"] = new GroupMatchMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3440`（`GroupMatchMethod`）

```text
GroupMatchMethod:
构造：返回类型 = long；
     参数 = [ArgTypeList { Any, VariadicSameAsFirst }]
     ; 第 1 参数任意类型，其余可变参数且类型须与第 1 参数相同
     CanRestructure = false。
（CheckArgumentType 的手工校验代码整体被注释掉，类型检查交给
  argumentTypeArrayEx 的 VariadicSameAsFirst 规则。）

GetIntValue(exm, args):
    ret = 0
    若 args[0] 的操作数类型 == long:      ; key 是整数
        baseValue = args[0].GetIntValue(exm)
        对 i = 1 .. args.Count-1:
            若 baseValue == args[i].GetIntValue(exm):
                ret += 1
    否则:                                  ; key 是字符串
        baseString = args[0].GetStrValue(exm)
        对 i = 1 .. args.Count-1:
            若 baseString == args[i].GetStrValue(exm):
                ret += 1
    返回 ret
```

## 备注

- ecd 文档只有 Expression.md 签名一览中的一行，没有详解小节与示例；以上语义完全由源码得出（整数/字符串双分支、相等计数）。
- 源码中 `MATCH`/`CMATCH`（MatchMethod）是「统计数组元素中等于值的个数」的相关函数，GROUPMATCH 则是对「逐个列出的参数」计数，注意区分。
- 参数为可变个数；实现要求后续参数与 key 类型一致（VariadicSameAsFirst），类型不一致属于解析期错误。
