# VARSIZE

- **类别**：式中函数（另有同名指令形态，见备注）
- **签名**：
  - `int VARSIZE(str name, int dim = 0)`
- **文档来源**：`ecd/docs/translation/Command.md`「### VARSIZE `<变量名>`」（该节记载的是指令形态，结果写入 `RESULT`）、`ecd/Expression.md` 函数目录（`int VARSIZE(str name, int dim = 0)`，函数形态）；zh 套件 `zh/Variable.md:532-552` 交叉核对（同样以指令形态记载）

## 语义

返回数组变量指定维的大小（元素个数）。第 1 参数是变量名的字符串（如 `"FLAG"`），第 2 参数是维编号（0 起，从左到右），省略时取第 0 维。CSV 变量的大小通常由 `VariableSize.csv` 指定。

参数引用的是数组变量自身而非元素，因此即使写 `VARSIZE("FLAG:-1")` 这类带索引甚至越界的写法（在指令形态下）也不会出错。函数形态下第 1 参数不是已定义的变量名时抛错；对非数组变量（0 维）取大小也会抛错。

## 用法

### `int VARSIZE(str name, int dim = 0)`
```erb
A = VARSIZE("FLAG")          ; A = 10000（未修改大小时）
A = VARSIZE("SAVESTR")       ; A = 100
A = VARSIZE("TALENT")        ; A = 1000
;多维变量：
A = VARSIZE("DITEMTYPE", 0)  ; 第 1 维大小
A = VARSIZE("DITEMTYPE", 1)  ; 第 2 维大小
```

### 指令形态 `VARSIZE <变量名>`
多维数组时各维大小从左到右依次存入 `RESULT:0`、`RESULT:1`、`RESULT:2`……
```erb
VARSIZE FLAG
PRINTFORML <TEST1> = {RESULT:0}   ; <TEST1> = 10000
VARSIZE SAVESTR
PRINTFORML <TEST2> = {RESULT:0}   ; <TEST2> = 100
VARSIZE TALENT
PRINTFORML <TEST3> = {RESULT:0}   ; <TEST3> = 1000
```

## 源码实现（emuera.em/Emuera）

- 注册（函数）：`Runtime/Script/Statements/Function/Creator.cs:37`（`["VARSIZE"] = new VarsizeMethod()`）
- 实现（函数）：`Runtime/Script/Statements/Function/Creator.Method.cs:2333`（`VarsizeMethod`），实际取大小转调 `Runtime/Script/Statements/Variable/VariableToken.cs` 各派生类的 `GetLength(dimension)`
- 注册（指令）：`Runtime/Script/Statements/FunctionIdentifier.cs:399`（`argb[FunctionArgType.SP_VAR]`）
- 实现（指令）：`Runtime/Script/Process.ScriptProc.cs:316`（case `FunctionCode.VARSIZE`）→ `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1691`（`VarSize`）

```text
# 函数形态 VarsizeMethod.GetIntValue
函数 VARSIZE(name, dim?):
    var = IdentifierDictionary.GetVariableToken(name, null, true)   # 允许局部变量解析
    若 var == null:
        抛 CodeEE("VARSIZEの1番目の引数(\"name\")が変数名ではありません")
    d = 0
    若给了第2参数: d = (int)第2参数
    若 Config.VarsizeDimConfig 且 d > 0:
        d--      # EE 配置「VARSIZEの次元指定をERD機能に合わせる」：维号改为 1 起计数
    返回 var.GetLength(d)
        # GetLength 基类/派生类：非数组(0维)变量抛 CodeEE（不能获取0维变量的大小）；
        # 角色变量 sizes.Length==0 或维号越界时同样抛 CodeEE；
        # 否则返回 sizes[d]

# UniqueRestructure（常量折叠判断）:
    若第1参数是 SingleTerm 且（只有1参数 或 第2参数也是 SingleTerm）:
        查变量 token；变量不存在或为可变长引用类型时不能定数化，返回 false
        否则返回 true（允许把整个调用折叠为常量）

# 指令形态 ScriptProc switch-case → VariableEvaluator.VarSize(varID):
    resultArray = RESULT_ARRAY
    若 varID 是 2 维数组: RESULT:0 = GetLength(0); RESULT:1 = GetLength(1)
    否则若 varID 是 3 维数组: RESULT:0..2 = GetLength(0..2)
    否则: RESULT:0 = varID.GetLength()   # 1 维取 GetLength() 无参重载
```

## 备注

- **同名两种形态并存**：指令形态（`Runtime/Script/Statements/FunctionIdentifier.cs:399`，结果写入 `RESULT:0` 起，多维时一次填多格）与式中函数形态（`Runtime/Script/Statements/Function/Creator.cs:37`，一次只取一维并返回值）。ecd `Command.md` 与 zh `Variable.md` 记载的都是指令形态；函数形态只出现在 `Expression.md` 目录中，文档未给出其错误行为。
- **函数形态可指定维号**（第 2 参数 `dim`），指令形态则自动填满 `RESULT` 各格；二者能力不同。
- **EE 扩展**：`Config.VarsizeDimConfig`（配置项「VARSIZEの次元指定をERD機能に合わせる / Imitate ERD to VARSIZE dimension specification」，默认关）开启后，函数形态的维号按 1 起计数（`dim > 0` 时减 1）。文档未记载。
- **错误行为文档未记载**（由源码补）：变量名不存在抛 `CodeEE`；非数组变量、角色变量维号越界也抛 `CodeEE`（如 `GetSize0DVar`「配列変数でない…」、`GetSizeCharaVarWithoutDim` 等）。
- `HasUniqueRestructure = true`：变量名与维号均为常量且变量非可变长引用时，整个调用在解析期折叠为常量（源码注释：1808beta009 引用型变量加入后需要排除可变长变量）。
