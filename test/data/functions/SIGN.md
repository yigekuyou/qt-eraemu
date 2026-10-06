# SIGN

- **类别**：式中函数（另有同名命令形态）
- **签名**：int SIGN(int n)
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；`ecd/Command.md`「### SIGN `<数值表达式>`」（命令形态）

## 语义

返回参数的符号：负值为 `-1`，0 为 `0`，正值为 `1`。

同名命令形态 `SIGN <数值表达式>` 把参数符号赋值给 `RESULT:0`，语义与函数形态相同。本文档以式中函数形态为主。

## 用法

### int SIGN(int n)
- `n`：整数表达式。
- 返回值：`n < 0` 时 `-1`；`n == 0` 时 `0`；`n > 0` 时 `1`。
```erb
	A = SIGN(-5)	; A = -1
	A = SIGN(0)		; A = 0
	A = SIGN(123)	; A = 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:84`（`["SIGN"] = new SignMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3218`（`SignMethod`）

```text
构造：返回类型 = long；参数 = [long]；CanRestructure = true（参数全为常量时可折叠）。

GetIntValue(exm, args):
    ret ← args[0].GetIntValue(exm)
    返回 Math.Sign(ret)
```

## 备注

- ecd/Command.md 小节描述的是命令形态（赋给 `RESULT:0`），ecd/Expression.md 只给出函数签名；两者语义一致。
- 无错误分支（`Math.Sign(long)` 对任何 long 值都有定义）。
- zh 套件未收录本函数。
