# CURRENTALIGN

- **类别**：式中函数
- **签名**：str CURRENTALIGN()
- **文档来源**：`ecd/Command.md`「### CURRENTALIGN」小节；`ecd/Expression.md`「内置表达式内函数一览」`str CURRENTALIGN()`；zh 套件未收录

## 语义

把当前文字对齐方式作为大写字符串返回。返回值与 `ALIGNMENT` 指令的参数格式相同，只能是 `"LEFT"`、`"CENTER"`、`"RIGHT"` 之一。若从未使用过 `ALIGNMENT` 指令，则返回默认对齐方式 `"LEFT"`。无参数、无副作用；读取的是控制台当前的对齐状态，因此不可常量折叠（`CanRestructure = false`）。

## 用法

### str CURRENTALIGN()
- 无参数（`()` 不能省略，以与变量区分）。
- 返回值：当前对齐方式，`"LEFT"` / `"CENTER"` / `"RIGHT"`。
```erb
ALIGNMENT CENTER
IF CURRENTALIGN() == "CENTER"
    PRINTL 当前是居中对齐
ENDIF
ALIGNMENT LEFT
PRINTL %CURRENTALIGN()%      ; 输出 LEFT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:51`（`["CURRENTALIGN"] = new CurrentAlignMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2678`（`CurrentAlignMethod`）

```text
CurrentAlignMethod:
构造：返回类型 = string；参数表 = []（无参）；CanRestructure = false。
GetStrValue(exm, args):
    若 exm.Console.Alignment == DisplayLineAlignment.LEFT   → 返回 "LEFT"
    否则若 exm.Console.Alignment == DisplayLineAlignment.CENTER → 返回 "CENTER"
    否则                                              → 返回 "RIGHT"
```

## 备注

- ecd/Command.md 称结果「返回到 RESULTS:0 中」，本函数实际为式中函数，字符串直接作为表达式的值返回；Expression.md 的 `str CURRENTALIGN()` 与源码一致。
- 源码中 `else` 分支把除 LEFT/CENTER 之外的一切对齐状态都归为 "RIGHT"（如未来枚举增加新值，也会被报告为 RIGHT）。
