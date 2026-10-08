# GETTIME

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（另有同名命令形态，本仓库两者均已注册）
- **签名**：int GETTIME()
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；`ecd/Command.md`「## 日期·时间的获取」→「### GETTIME」（命令形态）；zh 套件未收录

## 语义

取得计算机当前的日期时间信息。

- 式中函数形态：返回形如 `YYYYMMDDhhmmssmmm` 的 17 位整数（年、月、日、时、分、秒、毫秒依次拼接）。例如 2009 年 3 月 28 日 13 时 5 分 23 秒 678 毫秒时返回 `20090328130523678`。
- 同名命令形态 `GETTIME`：把上述整数赋值给 `RESULT:0`，同时把格式化字符串 `yyyy/MM/dd HH:mm:ss`（如 `2009/03/28 13:05:23`）赋值给 `RESULTS:0`。

文档说明：`RESULTS:0` 主要设想用于存档注释；如需自定义年月日的表示方式，请分解使用 `RESULT:0`。`RESULT:0` 的精度取决于运行环境，约为十几到几十毫秒（只经过数毫秒时可能返回相同的值），以性能测量为目的时请注意（测经过时间建议改用 `GETMILLISECOND`/`GETSECOND`）。

函数形态只返回整数（不含字符串）；命令形态一次给出两个结果，这也是源码注释「2つに代入する必要があるのでMETHOD化できない」（需要赋值两个结果，无法 METHOD 化）的原因。

## 用法

### int GETTIME()
- 无参数（`()` 必须写，以区别于变量）。
- 返回值：17 位整数 `YYYYMMDDhhmmssmmm`。
```erb
DATE = GETTIME()
YEAR = DATE / 10000000000000
MONTH = DATE / 100000000000 % 100
PRINTFORML 今天是 {YEAR} 年 {MONTH} 月
```
### 命令形态：GETTIME（无参数）
- 副作用：`RESULT:0` ← 17 位日期时间整数；`RESULTS:0` ← `"yyyy/MM/dd HH:mm:ss"` 格式字符串。
```erb
GETTIME
PRINTFORML 现在：{RESULTS:0}
```

## 源码实现（emuera.em/Emuera）

- 注册（函数）：`Runtime/Script/Statements/Function/Creator.cs:68`（`["GETTIME"] = new GettimeMethod()`）
- 实现（函数）：`Runtime/Script/Statements/Function/Creator.Method.cs:2883`（`GettimeMethod`）
- 注册（命令）：`Runtime/Script/Statements/FunctionIdentifier.cs:400`（`argb[VOID], METHOD_SAFE | EXTENDED`）
- 实现（命令）：`Runtime/Script/Process.ScriptProc.cs:383`（switch-case `FunctionCode.GETTIME`）

```text
GettimeMethod（函数形态）:
构造：返回类型 = long；参数 = []；CanRestructure = false。
GetIntValue(exm, args):
    date ← DateTime.Now.Year
    date ← date * 100 + DateTime.Now.Month
    date ← date * 100 + DateTime.Now.Day
    date ← date * 100 + DateTime.Now.Hour
    date ← date * 100 + DateTime.Now.Minute
    date ← date * 100 + DateTime.Now.Second
    date ← date * 1000 + DateTime.Now.Millisecond
    返回 date        # 17 位，约 2 京

命令形态（ScriptProc.cs:383）:
    vEvaluator.RESULT  ← 上述 17 位整数（同一算法逐次读取 DateTime.Now）
    vEvaluator.RESULTS ← DateTime.Now.ToString("yyyy/MM/dd HH:mm:ss")
```

## 备注

- **文档与源码差异（RESULTS 格式）**：ecd/Command.md 写 `RESULTS:0` 为 `"2009年03月28日 13:05:23"`；源码实际为 `DateTime.Now.ToString("yyyy/MM/dd HH:mm:ss")`，即 `"2009/03/28 13:05:23"`（斜杠分隔、无"年/月/日"汉字）。文档描述疑似沿袭 eramaker/旧版 Emuera 行为，两边如实记录。
- 函数形态每次乘加都重新读取 `DateTime.Now`，极端情况下（跨毫秒/跨秒）各字段可能来自略微不同的时刻；命令形态同理。
- GETTIME 是本批函数中唯一同时注册了命令形态的名字（`Runtime/Script/Statements/FunctionIdentifier.cs:400`）；GETMILLISECOND/GETSECOND/GETTIMES 仅有函数形态。
- 精度警告（十几到几十毫秒）来自文档。
- zh 套件未收录本函数。
