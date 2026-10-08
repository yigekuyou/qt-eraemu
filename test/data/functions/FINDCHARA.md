# FINDCHARA

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数
- **签名**：int FINDCHARA(var key, ? value, int start = 0, int end = ※)
- **文档来源**：`ecd/Command.md`「### FINDCHARA `<角色变量>`, `<式>`(, `<数值表达式>`, `<数值表达式>`)」小节；`ecd/Expression.md`「内置表达式内函数一览」`int FINDCHARA(var key, ? value, int start = 0, int end = ※)`；zh 套件未收录

## 语义

在已登录的角色中检索「角色变量 `key` 的指定元素等于 `value`」的角色，返回其登录编号（角色列表中的位置，即 `CHARANUM` 维度的下标）。存在多个命中时返回最先命中的角色（`FINDLASTCHARA` 返回最后命中的）。未找到返回 `-1`。

- 第 1 参数必须是角色变量（`IsCharacterData` 的变量，如 `CFLAG`、`NAME`、`TALENT`），可带元素下标（如 `CFLAG:10`）；多维角色变量按项给出的下标定位第 2~3 维，检索沿角色编号维进行。
- 第 2 参数类型由第 1 参数决定（数值变量配数值、字符串变量配字符串），字符串按整串相等比较。
- 第 3 参数指定起始角色位置（默认 0），第 4 参数指定结束位置（默认 `CHARANUM`）。

错误行为：
- 第 3 参数 `< 0` 或 `>= CHARANUM`：抛出 CodeEE「{0}関数: 第{1}引数({2})はキャラクタ位置の範囲外です」。
- 第 4 参数 `< 0` 或 `> CHARANUM`：同样抛出上述错误。
- `start >= end` 时不报错，直接返回 `-1`。

## 用法

### int FINDCHARA(var key, ? value, int start = 0, int end = ※)
- `key`：角色变量（可带元素下标），决定在哪个变量上检索。
- `value`：要检索的值，类型与 `key` 的元素类型一致。
- `start`：检索起始角色位置，默认 0。
- `end`：检索结束位置（不含），默认当前角色数 `CHARANUM`。
- 返回值：命中的角色登录编号，未找到为 `-1`。
```erb
; 检索 CFLAG:10 == 123 的所有角色
X = -1
WHILE 1
    FINDCHARA CFLAG:10, 123, X + 1
    X = RESULT
    SIF X < 0
        BREAK
    PRINTFORML %NAME:X%
WEND
; 取 CSTR:0 == "爱丽丝" 的最后一名角色
NO = FINDLASTCHARA(CSTR:0, "爱丽丝")
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:32`（`["FINDCHARA"] = new FindcharaMethod(false)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2212`（`FindcharaMethod`，`isLast = false`）
- 转调：`Runtime/Script/Statements/Variable/VariableEvaluator.cs:1257`（字符串版 `FindChara`）、`:1291`（数值版 `FindChara`）

```text
FindcharaMethod:
构造：返回类型 = long；
     参数表 = [角色变量(任意型), 与第1参数同型, int, int]（第 3、4 参数可省略）；
     CanRestructure = false；isLast = false。
GetIntValue(exm, args):
    vTerm ← (VariableTerm)args[0]
    elem ← 0
    若 vTerm 标识符为一维（角色变量带 1 个元素下标）:  elem ← 下标1
    否则若为二维: elem ← (下标1 << 32) + 下标2        ; 打包两个下标
    startindex ← (args.Count >= 3 且 args[2] != null) ? args[2] : 0
    lastindex  ← (args.Count >= 4 且 args[3] != null) ? args[3] : CHARANUM
    若 startindex < 0 或 startindex >= CHARANUM: 抛出 CodeEE("...第3引数(...)はキャラクタ位置の範囲外です")
    若 lastindex  < 0 或 lastindex  >  CHARANUM: 抛出 CodeEE("...第4引数(...)はキャラクタ位置の範囲外です")
    若 varID 为字符串变量: word ← args[1].GetStrValue(exm)
    否则:                word ← args[1].GetIntValue(exm)
    返回 VariableEvaluator.FindChara(varID, elem, word, startindex, lastindex, isLast)

VariableEvaluator.FindChara(varID, elem64, word, startIndex, lastIndex, isLast):
    若 startIndex >= lastIndex: 返回 -1
    fvp ← 指向 varID 的固定元素项
    一维 → fvp.Index2 ← elem64
    二维 → fvp.Index2 ← elem64 >> 32; fvp.Index3 ← elem64 & 0x7FFFFFFF
    若 isLast: 对 i 从 lastIndex-1 降到 startIndex: fvp.Index1 ← i; 若 word == fvp 值: 返回 i
    否则:      对 i 从 startIndex 到 lastIndex-1:  fvp.Index1 ← i; 若 word == fvp 值: 返回 i
    返回 -1
```

## 备注

- ecd/Command.md 以命令口吻称结果「返回给 RESULT:0」（其示例也用 `FINDCHARA ...` 独立行 + `X = RESULT` 的写法），本函数实际为式中函数，可直接写 `X = FINDCHARA(...)`；两种写法在本仓库均可用，但文档主体按式中函数收录。
- Expression.md 中 start/end 的默认值写作 `0` 与 `※`（※ = 数组大小，此处即 `CHARANUM`），与源码一致。
- ecd/Command.md 说「查找范围超过角色数量范围时会出错」；源码的具体边界为：start 允许 `[0, CHARANUM-1]`，end 允许 `[0, CHARANUM]`，且 `start >= end` 恒返回 -1 而非报错。
- 字符串比较使用 `==` 整串相等，不支持正则（与 FINDELEMENT 不同）。
