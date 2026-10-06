# GETNUM

- **类别**：式中函数（ecd/Command.md 以命令口径收录；本仓库仅有函数形态）
- **签名**：int GETNUM(var key, str name)
- **签名（EE 扩展，源码支持）**：int GETNUM(var key, str name, int erdIndex)
- **文档来源**：`ecd/Command.md`「### GETNUM `<变量名>`, `<字符串表达式>`」；`ecd/Expression.md`（表达式内函数签名列表）；zh 套件未收录

## 语义

把 CSV 变量（如 `abl.csv`、`talent.csv` 等）或 EE 关键字索引变量中的"文本索引"转换成对应的"数值索引"并返回。

例如 `abl.csv` 中定义了 `2,技巧`，则 `GETNUM ABL, "技巧"` 返回 `2`。当文本索引没有定义时返回 `-1`。空字符串键也视为未定义，返回 `-1`。

第 1 参数必须是变量（变量引用，如 `ABL`、`TALENT`），不能是字符串。EE 扩展：第 3 参数用于带关键字索引（ERD）的用户定义变量，此时以 `变量名@第3参数值` 的名字在 ERD 词典中检索。

## 用法

### int GETNUM(var key, str name)
- `key`：变量引用（如 `ABL`、`EXP` 等 CSV 对应变量）。
- `name`：字符串表达式，CSV 中定义的文本索引（名称列）。
- 返回值：对应的数值索引；未定义时 `-1`。
```erb
;abl.csv 中定义了 "2,技巧"
A = GETNUM(ABL, "技巧")       ; A = 2
PRINTFORML 技巧 Lv{ABL:GETNUM(ABL, "技巧")}
```
### int GETNUM(var key, str name, int erdIndex)（EE 扩展）
- `erdIndex`：整数，用于构造 ERD 词典名 `变量名@erdIndex`，在用户定义的关键字索引变量中检索。
```erb
;EE 关键字索引变量（ERD）的文本键 → 数值索引
A = GETNUM(MYVAR, "钥匙", 0)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:100`（`["GETNUM"] = new GetnumMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3725`（`GetnumMethod`）；转调 `Runtime/Script/Data/ConstantData.cs:830`（`ConstantData.TryKeywordToInteger`）

```text
构造：返回类型 = long；
     参数 = [(RefAny|AllowConstRef) 变量引用, string, int(自第 2 个参数起可省略)]；
     CanRestructure = true，HasUniqueRestructure = true。

GetIntValue(exm, args):
    vToken ← (VariableTerm)args[0]          # 第 1 参数必须是变量项
    varCode ← vToken.Identifier.Code
    varname ← vToken.Identifier.Name
    若 args.Count > 2:                       # EE_ERD 扩展
        varname ← vToken.Identifier.Name + "@" + args[2].GetIntValue(exm)
    key ← args[1].GetStrValue(exm)
    若 exm.VEvaluator.Constant.TryKeywordToInteger(out ret, varCode, key, -1, varname):
        返回 ret
    否则:
        返回 -1

UniqueRestructure(exm, args):                # 常量折叠：key 为常量时整式可折叠
    args[1] ← args[1].Restructure(exm)
    返回 args[1] 是 SingleTerm

TryKeywordToInteger(ret, code, key, index, varname):   # ConstantData.cs:830
    ret ← 0
    若 key 为 null 或空字符串: 返回 false
    dic ← 该变量 code（及 index）对应的关键字→整数词典（通常来自 csv 的名称列）
    若 dic 存在且含 key: ret ← dic[key]，返回 true
    若 varname 非空:
        在 ERD 词典 erdNameToIntDics 中查 varname 对应的词典
        若找到且含 key: ret ← dic[key]，返回 true
    返回 false
```

## 备注

- ecd/Command.md 的小节按"返回到 `RESULT:0`"的命令口径描述（这是原版 Emuera 命令形态的说明）；本仓库 `Runtime/Script/Statements/FunctionIdentifier.cs` 中没有 GETNUM 命令注册，只有式中函数形态，返回值直接在表达式中使用。
- 源码支持的第 3 参数（ERD 关键字索引）两套文档均未记载，属 EE 扩展。
- 未定义时文档与源码一致返回 `-1`；源码另将空字符串键也按未定义处理（返回 `-1`），文档未提及。
- zh 套件未收录本函数。
