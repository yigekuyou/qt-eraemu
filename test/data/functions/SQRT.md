# SQRT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数
- **签名**：
  - int SQRT(int n)
- **文档来源**：`ecd/docs/translation/Command.md`「SQRT `<数值表达式>`」小节、`ecd/docs/translation/Expression.md` 内置函数目录「int SQRT(int n)」；zh 套件未收录

## 语义

返回参数的平方根，结果向下取整为 64 位整数（即 floor(√n)）。

参数为负数时抛出运行时错误（CodeEE：「SQRT 関数の引数に負の値が指定されました」/ 参数为负）。EraBasic 没有浮点运算，SQRT 的结果为整数截断，例如 `SQRT(10)` 得 3。

作为式中函数可在任何表达式中使用；其两个常量参数可在编译期折叠（CanRestructure = true）。

## 用法

### SQRT(n)
- n：非负整数表达式。
```erb
A = SQRT(10)          ;A = 3
B = SQRT(16)          ;B = 4
IF SQRT(AREA) > 100
	PRINTL 边长超过 100
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:79`（`["SQRT"] = new SqrtMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3112`（`private sealed class SqrtMethod : FunctionMethod`，`GetIntValue`）

```text
函数 SQRT(参数表):
    ret <- 参数[0] 的整数值
    若 ret < 0:
        抛出 CodeEE（"{0}関数:第{1}引数に負の値({2})が指定されました"）
    返回 (long)Math.Sqrt(ret)    # double 平方根后向零截断
                                 # 对非负输入等价于向下取整 floor(√n)
```

## 备注

- ecd Command.md 把 SQRT 写在按「命令」风格组织的章节里（描述为「把参数的平方根赋值给 RESULT:0」），这是旧版描述口吻；本仓库中它注册为式中函数，返回值直接参与表达式，并不写 RESULT。
- 实现使用 `Math.Sqrt`（double 精度）。对极大整数（接近 long.MaxValue），double 平方根可能存在 ±1 的舍入边界误差，文档未提及此点。
- zh 套件未收录 SQRT 条目。
