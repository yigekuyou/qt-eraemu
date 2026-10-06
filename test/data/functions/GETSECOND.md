# GETSECOND

- **类别**：式中函数
- **签名**：int GETSECOND()
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；`ecd/Command.md`「## 日期·时间的获取」→「### GETSECOND」；zh 套件未收录

## 语义

返回自公元 0001 年 1 月 1 日起经过的秒数（基于当前系统时间 `DateTime.Now`）。无参数，但 `()` 必须写。

由于返回值可以直接加减，比 `GETTIME` 更适合测量经过时间等用途。适合秒级精度的计时；毫秒级需求请用 `GETMILLISECOND`。函数不产生其他副作用。

## 用法

### int GETSECOND()
- 无参数（`()` 必须写，以区别于变量）。
- 返回值：自 0001-01-01 起经过的秒数。
```erb
T1 = GETSECOND()
;……执行一些处理……
T2 = GETSECOND()
PRINTFORML 处理耗时 {T2 - T1} 秒
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:71`（`["GETSECOND"] = new GetSecondMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2933`（`GetSecondMethod`）

```text
构造：返回类型 = long；参数 = []（无参数）；CanRestructure = false（不可常量折叠）。

GetIntValue(exm, args):
    # 西历 0001 年 1 月 1 日以来的经过时间，单位秒
    # Ticks 是 100 纳秒单位，但实际上并没有那种精度，所以除以 10^7 即可
    返回 DateTime.Now.Ticks / 10000000
```

## 备注

- ecd/Command.md 的小节按"赋值给 `RESULT:0`"的命令口径描述；但本仓库中 GETSECOND 只注册为式中函数（`Runtime/Script/Statements/FunctionIdentifier.cs` 中无同名命令注册），使用时应直接在表达式中取返回值。
- 与 `GETMILLISECOND`（`Ticks / 10000`）仅除数不同；源码注释指出 Ticks 的名义精度远高于实际系统时钟精度。
- zh 套件未收录本函数。
