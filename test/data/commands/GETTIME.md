# GETTIME

- **类别**：命令
- **签名**：`GETTIME`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md`「日期·时间的获取」`### GETTIME` 小节；`Era-Chinese-Documentation`（zh 套件）未收录该命令

## 语义

把计算机当前日期时间写入 `RESULT:0`（数值）和 `RESULTS:0`（字符串）。`RESULT:0` 是形如 `20090328130523678` 的 17 位数字（年月日时分秒毫秒依次拼接），精度受运行环境影响，约十几到几十毫秒，短间隔连续调用可能得到相同值，不宜作为高精度计时（计时请用 `GETMILLISECOND`）。`RESULTS:0` 是格式化的日期时间字符串，主要设想用于存档注释。注意：另有一个同名的**式中函数** `GETTIME()`（只返回同格式的数值，见备注）。

## 用法

### GETTIME
无参数；结果写入 `RESULT:0` 与 `RESULTS:0`。

```erb
GETTIME
PRINTFORL RESULT:0        ;例如 20090328130523678
PRINTFORL RESULTS:0       ;例如 2009/03/28 13:05:23
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:400`（`argb[FunctionArgType.VOID]`，flag = METHOD_SAFE | EXTENDED；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:141`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:383`（switch-case，无独立类）

```text
case GETTIME:
    date ← DateTime.Now.Year
    date ← date * 100 + Month
    date ← date * 100 + Day
    date ← date * 100 + Hour
    date ← date * 100 + Minute
    date ← date * 100 + Second
    date ← date * 1000 + Millisecond      # 17 位数字（约 2 京）
    vEvaluator.RESULT  ← date
    vEvaluator.RESULTS ← DateTime.Now.ToString("yyyy/MM/dd HH:mm:ss")
```

## 备注

- **文档与源码不一致**：ecd 文档写 `RESULTS:0` 形如 `"2009年03月28日 13:05:23"`（汉字"年/月/日"），源码实际格式串为 `"yyyy/MM/dd HH:mm:ss"`，即输出 `"2009/03/28 13:05:23"`（斜杠分隔、无汉字）。以源码为准。
- `RESULT:0` 的 17 位数值格式两文档一致（源码注释"17桁。2京くらい。"）。
- 同名式中函数：`GETTIME()` 注册于 `Runtime/Script/Statements/Function/Creator.cs:68`，实现为 `Runtime/Script/Statements/Function/Creator.Method.cs:2883` 的 `GettimeMethod`（无参、返回 long，计算方式与命令完全相同）；另有字符串版 `GETTIMES()`（`Runtime/Script/Statements/Function/Creator.Method.cs:2904`）。本文件只覆盖命令版。
- 其余无冲突。
