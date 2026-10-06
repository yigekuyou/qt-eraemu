# GETLINESTR

- **类别**：式中函数
- **签名**：str GETLINESTR(str s)
- **文档来源**：ecd/Expression.md 未收录；ecd/Command.md 未收录（仅 `ecd/Error_Index.md` 收录其运行期错误消息「GETLINESTR関数の引数が空文字列です」）；zh 套件未收录。语义完全依据源码推导。

## 语义

生成一条"横线字符串"：把参数字符串 `s` 反复拼接，直到总显示宽度达到当前可绘制宽度（`Config.DrawableWidth`，即一行能容纳的像素宽度），并在超出时逐字符削减，使结果恰好不超过一行宽度。用于实现与 `DRAWLINE`/`CUSTOMDRAWLINE` 指令相同效果的"自定义填充字符的横线"，但以函数形态在表达式中求值，结果作为字符串返回，可再加工或用 `PRINT` 系指令输出。

参数为空字符串时抛出 CodeEE 运行期错误（第 1 参数为空字符串）。宽度测量使用默认字体（`Config.DefaultFont`）的显示宽度。

## 用法

### str GETLINESTR(str s)
- `s`：字符串表达式，作为横线的重复单元（可以是多字符字符串）。
- 返回值：重复 `s` 填满一行宽度的字符串。
```erb
;用 "─" 填满一行，等价于 DRAWLINE "─"
PRINTL %GETLINESTR("─")%
;用任意字符串做分隔线
PRINTL %GETLINESTR("=-")%
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:135`（`["GETLINESTR"] = new GetLineStrMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4869`（`GetLineStrMethod`，本类为 public）；转调 `UI/Game/EmueraConsole.Print.cs:726`（`EmueraConsole.getStBar(string)`）

```text
构造：返回类型 = string；参数 = [string]；CanRestructure = true（常量折叠允许）。

GetStrValue(exm, args):
    str ← args[0].GetStrValue(exm)
    若 str 为 null 或空字符串:
        抛出 CodeEE（"GETLINESTR 函数的第 1 参数为空字符串"）
    返回 exm.Console.getStBar(str)

getStBar(barStr):                       # EmueraConsole.Print.cs:726
    builder ← 新 StringBuilder，先追加一次 barStr
    width ← 0
    当 width < Config.DrawableWidth:            # 逐个追加直到超过行宽
        builder 追加 barStr
        width ← stringMeasure.GetDisplayLength(builder.ToString(), Config.DefaultFont)
    当 width > Config.DrawableWidth:            # 超出后逐字符删除直到不再超出
        builder 删除末尾 1 个字符
        width ← stringMeasure.GetDisplayLength(builder.ToString(), Config.DefaultFont)
    返回 builder.ToString()
```

## 备注

- 两套文档（ecd、zh）均未收录本函数，签名 `str GETLINESTR(str s)` 依据源码构造函数（`argumentTypeArray = [typeof(string)]`）与实现得出。
- `DRAWLINE`/`CUSTOMDRAWLINE` 指令（含 `Runtime/Script/Statements/Instraction.Child.cs:523`、`:1228` 处的 PRINT 系条线指令）与 GETLINESTR 共用同一个 `getStBar`，因此 GETLINESTR 可视为这些指令的函数形态。
- 追加阶段每追加一个完整 `barStr` 测量一次；若 `s` 是多字符字符串，最后的删除阶段按单字符削减以对齐行宽。
- 返回值的显示宽度不保证恰好等于 DrawableWidth（半角字符可能留有余量）。
