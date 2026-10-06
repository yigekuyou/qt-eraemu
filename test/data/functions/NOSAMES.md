# NOSAMES

- **类别**：式中函数
- **签名**：int NOSAMES(? value1, ? value2...)
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；`ecd/Command.md` 未收录

## 语义

可变参数函数，判断所有参数是否两两不同（互不重复）。所有参数值去重后的个数若少于参数个数（即存在重复），返回 `0`；全部互不相同则返回 `1`。

参数类型由第一参数决定，后续参数必须与第一参数同类型（数值或字符串均可，`?` 表示两者皆可）。至少需要 2 个参数。

与 `ALLSAMES`（判断所有参数是否全部相同）互为对照。

## 用法

### int NOSAMES(? value1, ? value2...)
- `value1`：第一个比较值，类型决定后续参数类型。
- `value2...`：后续比较值，类型必须与第一参数相同，数量不限。
- 返回值：全部参数两两不同时 `1`，存在重复时 `0`。
```erb
	IF NOSAMES(A, B, C)
		PRINTL A、B、C 互不相同。
	ELSE
		PRINTL A、B、C 中存在重复值。
	ENDIF
	IF NOSAMES("甲", "乙", "丙")
		PRINTL 字符串参数同样可用。
	ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:93`（`["NOSAMES"] = new NosamesMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3492`（`NosamesMethod`）

```text
构造：返回类型 = long；参数 = 第一参数 Any，后续 VariadicSameAsFirst（类型须与第一参数一致，至少 2 个）；CanRestructure = false。

GetIntValue(exm, args):
    若 args[0].GetOperandType() == long:
        valueArray ← 各 args[i].GetIntValue(exm) 收集为 long[]
        若 valueArray.Distinct() 的个数 != args.Count:
            返回 0
    否则（字符串参数）:
        stringArray ← 各 args[i].GetStrValue(exm) 收集为 string[]
        若 stringArray.Distinct() 的个数 != args.Count:
            返回 0
    返回 1
```

## 备注

- ecd/Command.md 未收录 NOSAMES；`ecd/Expression.md` 只在函数一览中给出签名 `int NOSAMES(? value1, ? value2...)`，未附语义说明。本文档语义依据源码撰写。
- 源码用 `Distinct()` 去重比较，字符串比较为区分大小写的普通相等比较。
- 同章节的 `ALLSAMES` 为其姊妹函数（判断全部相同）。
- zh 套件未收录本函数。
