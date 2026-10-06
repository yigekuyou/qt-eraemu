# GETMILLISECOND

- **类别**：式中函数
- **签名**：int GETMILLISECOND()
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；`ecd/Command.md`「## 日期·时间的获取」→「### GETMILLISECOND」；zh 套件未收录

## 语义

返回自公元 0001 年 1 月 1 日起经过的毫秒数（基于当前系统时间 `DateTime.Now`）。无参数，但 `()` 必须写。

由于返回值可以直接加减，比 `GETTIME` 更适合测量经过时间等用途。文档提示其精度取决于运行环境，约为十几到几十毫秒（仅经过数毫秒时可能返回相同的值），以性能测量为目的时请注意。函数不产生其他副作用。

## 用法

### int GETMILLISECOND()
- 无参数（`()` 必须写，以区别于变量）。
- 返回值：自 0001-01-01 起经过的毫秒数。
```erb
T1 = GETMILLISECOND()
;……执行一些处理……
T2 = GETMILLISECOND()
PRINTFORML 处理耗时 {T2 - T1} 毫秒
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:70`（`["GETMILLISECOND"] = new GetmsMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2918`（`GetmsMethod`）

```text
构造：返回类型 = long；参数 = []（无参数）；CanRestructure = false（不可常量折叠）。

GetIntValue(exm, args):
    # 西历 0001 年 1 月 1 日以来的经过时间，单位毫秒
    返回 DateTime.Now.Ticks / 10000      # 1 Tick = 100 纳秒
```

## 备注

- ecd/Command.md 的小节按"赋值给 `RESULT:0`"的命令口径描述；但本仓库中 GETMILLISECOND 只注册为式中函数（`Runtime/Script/Statements/FunctionIdentifier.cs` 中无同名命令注册），使用时应直接在表达式中取返回值。
- 本仓库无 GETMILLISECOND 命令形态，因此不存在把结果写入 `RESULT:0` 的行为。
- 精度警告（十几到几十毫秒）来自文档；源码中 `Ticks / 10000` 本身不做额外精度保证。
- zh 套件未收录本函数。
