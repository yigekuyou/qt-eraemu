# PRINTCLENGTH

- **类别**：式中函数
- **签名**：int PRINTCLENGTH()
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 未收录（仅依据源码与配置项撰写）

## 语义

无参数式中函数，返回当前配置中「PRINTC 文字数」（PRINTCの文字数）设置的值，即每个 `PRINTC` 项格式化后占用的字符宽度。配置项默认值为 25。

它常与 `PRINTCPERLINE`（一行并列的 PRINTC 个数）配合，用于在脚本中计算按钮排布宽度。无副作用，不会抛错。

## 用法

### int PRINTCLENGTH()
- 无参数（`()` 必须写，以区分变量）。
- 返回值：配置项「PRINTC 文字数」（默认 25）。
```erb
PRINTFORML 每个按钮宽度 {PRINTCLENGTH()}，一行 {PRINTCPERLINE()} 个。
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:66`（`["PRINTCLENGTH"] = new PrintCLengthMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2855`（`PrintCLengthMethod`）

```text
构造：返回类型 = long；参数列表 = []；CanRestructure = true（允许常量折叠）。

GetIntValue(exm, args):
    返回 Config.PrintCLength
```

`Config.PrintCLength` 来自配置项 `PrintCLength`（`Runtime/Config/ConfigData.cs:65`，名称「PRINTCの文字数」，默认 25）。

## 备注

- ecd 两份主文档（Expression.md、Command.md）均未收录 PRINTCLENGTH，本文档语义依据源码实现与配置项定义撰写。
- PRINTCLENGTH 没有同名命令形态（不同于 PRINTCPERLINE，后者在 FunctionIdentifier.cs 另有命令式注册）。
- zh 套件未收录本函数。
