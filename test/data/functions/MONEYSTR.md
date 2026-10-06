# MONEYSTR

- **类别**：式中函数
- **签名**：
  - `str MONEYSTR(int value, str format = "")`
- **文档来源**：`ecd/Command.md`「显示处理·字体处理·显示方式参考」→「MONEYSTR」小节；`ecd/Expression.md`「内置表达式内函数一览」；zh 套件未收录

## 语义

把数值参数转换为表示金钱的字符串并返回。转换时会依据 emulator 的金钱设置（`Config` 的金钱单位与单位位置）把金钱单位添加在数字之前或之后。第 2 参数指定格式化数值的格式，与 `TOSTR` 的格式指示符用法相同（.NET 标准数值格式字符串）；格式串非法时抛出 CodeEE。省略第 2 参数时直接按十进制输出数字本身（不做千位分隔等格式化）。

## 用法

### MONEYSTR(int value, str format = "")

- `value`：金额数值表达式。
- `format`：格式指示符（字符串），可省略；省略（或为 `""`）时直接输出数字。

```erb
MONEYSTR(10000)                    ; 例：默认设置下 "$10000"（或 "10000$"，取决于设置）
PRINTFORML 所持金为{MONEYSTR(MONEY)}
PRINTFORML 所持金为{MONEYSTR(MONEY, "N0")}   ; 带千位分隔：" $10,000"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:64`（`["MONEYSTR"] = new MoneyStrMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2795`（`MoneyStrMethod`）

```text
function MONEYSTR(args, exm):      ; 返回 string
    money = args[0].GetIntValue(exm)
    if args.Count < 2 or args[1] == null:
        return Config.MoneyFirst ? Config.MoneyLabel + money.ToString()
                                 : money.ToString() + Config.MoneyLabel
    format = args[1].GetStrValue(exm)
    try:
        ret = money.ToString(format)
    catch FormatException:
        throw CodeEE("MONEYSTR 的第 2 参数格式指定不正确")
    return Config.MoneyFirst ? Config.MoneyLabel + ret : ret + Config.MoneyLabel
```

## 备注

- ecd 文档以命令形态记载（"返回到 RESULTS:0 中"）；实现在式中函数注册表中，式中调用直接得到返回值，不经过 `RESULTS:0`。
- 金钱单位与位置由 `EmueraEMUERETA.config`/`Config` 的金钱设置决定（`MoneyLabel`、`MoneyFirst`），本函数自身没有控制单位的参数。
- 格式串错误是运行期 CodeEE（`trerror.InvalidFormat`，指明第 2 参数），解析期不检查。
- `CanRestructure = true`：参数全为常量时可在解析期折叠。
