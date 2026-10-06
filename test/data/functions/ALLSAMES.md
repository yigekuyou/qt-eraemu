# ALLSAMES

- **类别**：式中函数
- **签名**：int ALLSAMES(? value1, ? value2...)
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表，仅一行签名）；`ecd/Command.md` 未收录；zh 套件未收录

## 语义

判断所有参数是否全部相等。接受 2 个以上任意类型的参数（可变参数，第 2 个起类型须与第 1 个相同）。

- 全部参数相等时返回 `1`。
- 有任意一个参数与第一个参数不同时返回 `0`。

参数类型在解析期检查：第 2 个及以后的参数必须与第 1 个参数类型一致（整数或字符串，`VariadicSameAsFirst`）。第 1 个参数为整数时按整数比较，为字符串时按字符串比较。

与 `NOSAMES`（是否存在互不相同的值）相对；与 `GROUPMATCH`（key 是否匹配任意一个候选值）用途不同。

## 用法

### int ALLSAMES(? value1, ? value2...)
- `value1`：基准值（整数或字符串）。
- `value2...`：与 `value1` 同类型的可变个数值。
- 返回值：全部相同为 `1`，否则为 `0`。
```erb
IF ALLSAMES(A, B, C)
	PRINTL A、B、C 全部相等
ENDIF
IF ALLSAMES("甲", LOCALS:0, LOCALS:1)
	PRINTL 三个字符串相同
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:94`（`["ALLSAMES"] = new AllsamesMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3547`（`AllsamesMethod`）

```text
构造：返回类型 = long；argumentTypeArrayEx = [{ Any, VariadicSameAsFirst }]
     （第 2 参数起可变且类型须与第 1 参数相同）；CanRestructure = false。

GetIntValue(exm, args):
    若 args[0] 的操作数类型是 long（整数）:
        baseValue ← args[0].GetIntValue(exm)
        对 i = 1 .. args.Count-1:
            若 baseValue != args[i].GetIntValue(exm): 返回 0
    否则（字符串）:
        baseValue ← args[0].GetStrValue(exm)
        对 i = 1 .. args.Count-1:
            若 baseValue != args[i].GetStrValue(exm): 返回 0
    返回 1
```

## 备注

- 两套中文文档均未给出语义说明，仅 ecd/Expression.md 签名列表收录；以上语义直接由源码翻译得出。
- 文档签名写作 `int ...`，实现返回类型为 `long`（EraBasic 整数统一为 64 位），无实质差异。
- 不做常量折叠（CanRestructure = false）。
