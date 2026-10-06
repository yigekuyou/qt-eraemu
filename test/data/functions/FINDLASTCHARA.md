# FINDLASTCHARA

- **类别**：式中函数
- **签名**：int FINDLASTCHARA(var key, ? value, int start = 0, int end = ※)
- **文档来源**：`ecd/Command.md`「### FINDLASTCHARA `<角色变量>`, `<式>`(, `<数值表达式>`, `<数值表达式>`)」小节；`ecd/Expression.md`「内置表达式内函数一览」`int FINDLASTCHARA(var key, ? value, int start = 0, int end = ※)`；zh 套件未收录

## 语义

与 `FINDCHARA` 相同的检索：在已登录角色中查找「角色变量 `key` 的指定元素等于 `value`」的角色并返回其登录编号；区别在于存在多个命中时 `FINDLASTCHARA` 返回**最后**命中的角色（从 `end-1` 向 `start` 反向扫描）。未找到返回 `-1`。

- 第 1 参数必须是角色变量，可带元素下标（如 `CSTR:0`）；第 2 参数类型由第 1 参数决定（字符串按整串相等比较，不支持正则）。
- 第 3 参数指定起始角色位置（默认 0），第 4 参数指定结束位置（默认 `CHARANUM`）。

错误行为（与 FINDCHARA 一致）：
- 第 3 参数 `< 0` 或 `>= CHARANUM`：抛出 CodeEE「{0}関数: 第{1}引数({2})はキャラクタ位置の範囲外です」。
- 第 4 参数 `< 0` 或 `> CHARANUM`：同样抛出上述错误。
- `start >= end` 时不报错，直接返回 `-1`。

## 用法

### int FINDLASTCHARA(var key, ? value, int start = 0, int end = ※)
- `key`：角色变量（可带元素下标）。
- `value`：要检索的值，类型与 `key` 的元素类型一致。
- `start`：检索起始角色位置，默认 0。
- `end`：检索结束位置（不含），默认当前角色数 `CHARANUM`。
- 返回值：最后命中的角色登录编号，未找到为 `-1`。
```erb
; 取 CSTR:0 == "爱丽丝" 的最后一名角色
NO = FINDLASTCHARA(CSTR:0, "爱丽丝")
IF NO >= 0
    PRINTFORML 编号{NO}是最后一名爱丽丝
ENDIF
; 等价于在 [0, CHARANUM) 内反向找 TALENT:5 == 1
PRINTV FINDLASTCHARA(TALENT:5, 1)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:33`（`["FINDLASTCHARA"] = new FindcharaMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2212`（`FindcharaMethod`，`isLast = true`）
- 转调：`Runtime/Script/Statements/Variable/VariableEvaluator.cs:1257`（字符串版 `FindChara`）、`:1291`（数值版 `FindChara`）

```text
FindcharaMethod:（与 FINDCHARA 共用同一类，仅 isLast = true）
GetIntValue(exm, args):
    vTerm ← (VariableTerm)args[0]
    elem ← 0
    一维 → elem ← 下标1；二维 → elem ← (下标1 << 32) + 下标2
    startindex ← (args.Count >= 3 且 args[2] != null) ? args[2] : 0
    lastindex  ← (args.Count >= 4 且 args[3] != null) ? args[3] : CHARANUM
    若 startindex ∉ [0, CHARANUM): 抛出 CodeEE("...第3引数(...)はキャラクタ位置の範囲外です")
    若 lastindex  ∉ [0, CHARANUM]: 抛出 CodeEE("...第4引数(...)はキャラクタ位置の範囲外です")
    word ← args[1] 的字符串值或数值（随 varID 类型）
    返回 VariableEvaluator.FindChara(varID, elem, word, startindex, lastindex, isLast=true)

VariableEvaluator.FindChara(..., isLast=true):
    若 startIndex >= lastIndex: 返回 -1
    fvp ← 指向 varID 的固定元素项（下标同上打包规则）
    对 i 从 lastIndex-1 降到 startIndex:
        fvp.Index1 ← i
        若 word == fvp 值（字符串比较或数值比较）: 返回 i
    返回 -1
```

## 备注

- ecd/Command.md 称「与 FINDCHARA 相同，但存在多个命中时返回最后命中的角色」，与源码一致：两者共用 `FindcharaMethod`，唯一差别是构造参数 `last` 决定的扫描方向。
- ecd/Command.md 以命令口吻描述结果返回到 RESULT:0；本函数为式中函数，可直接用于表达式。
- 与 `FINDLASTELEMENT`（对一般数组、支持正则/全词匹配）不同，本函数只针对角色变量且字符串为整串相等比较。
