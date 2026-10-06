# GETTIMES

- **类别**：式中函数
- **签名**：str GETTIMES()
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；ecd/Command.md 无独立小节（GETTIME 小节仅涉及其命令形态）；zh 套件未收录

## 语义

返回表示当前日期时间的字符串，格式为 `yyyy/MM/dd HH:mm:ss`（例如 `2009/03/28 13:05:23`）。无参数，但 `()` 必须写。

它等价于命令形态 `GETTIME` 写入 `RESULTS:0` 的那个字符串，但以函数形态直接在表达式中取得，适合用于存档注释、日志时间戳等。

## 用法

### str GETTIMES()
- 无参数（`()` 必须写，以区别于变量）。
- 返回值：`"yyyy/MM/dd HH:mm:ss"` 格式的当前时间字符串。
```erb
PRINTFORML 现在时刻：%GETTIMES()%
;常与 SAVEDATA 注释配合：
STR:0 = @"%GETTIMES()% {DAY}日目"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:69`（`["GETTIMES"] = new GettimesMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2904`（`GettimesMethod`）

```text
构造：返回类型 = string；参数 = []（无参数）；CanRestructure = false。

GetStrValue(exm, args):
    返回 DateTime.Now.ToString("yyyy/MM/dd HH:mm:ss")
```

## 备注

- ecd/Expression.md 只在签名列表给出 `str GETTIMES()`，无语义说明；语义依据源码（格式串 `yyyy/MM/dd HH:mm:ss`）。命令形态 `GETTIME` 写入 `RESULTS:0` 的字符串与之一致（见 GETTIME.md）。
- 注意 GETTIMES（字符串）与 GETTIME（整数）是两个不同函数，名字仅差一个 s。
- zh 套件未收录本函数。
