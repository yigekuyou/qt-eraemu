# LINEISEMPTY

- **类别**：式中函数
- **签名**：int LINEISEMPTY()
- **文档来源**：`ecd/Command.md`「### LINEISEMPTY」；`ecd/Expression.md` 签名列表

## 语义

判断当前行是否为空行，返回到表达式中：是空行返回 `1`，否则返回 `0`。

通常使用在 `PRINTL` 指令前，检查是否有必要换行（避免连续 `PRINTL` 产生多余空行）。无参数、无副作用；"空行"指当前打印缓冲区（printBuffer）为空，即本行尚未输出任何内容。

## 用法

### int LINEISEMPTY()
- 无参数。
- 返回值：当前行为空行时 `1`，否则 `0`。
```erb
IF LINEISEMPTY() == 0
  PRINTL                     ; 当前行非空，先换行再输出
ENDIF
PRINTL 分割线之后的内容
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:126`（`["LINEISEMPTY"] = new LineIsEmptyMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4584`（`LineIsEmptyMethod`）；判定属性在 `UI/Game/EmueraConsole.Print.cs:102`（`EmueraConsole.EmptyLine => printBuffer.IsEmpty`）

```text
构造：返回类型 = long；参数 = []（无参）；CanRestructure = false（依赖控制台运行期状态）。

GetIntValue(exm, args):
    返回 GlobalStatic.Console.EmptyLine ? 1 : 0
    ; EmptyLine 即当前打印缓冲区 printBuffer.IsEmpty
```

## 备注

- ecd/Command.md 与源码一致；"空行 = 打印缓冲区为空"这一精确含义来自源码。
- 相关指令：`LINECOUNT`（统计显示行数）、`CLEARLINE`。
- zh 套件未收录本函数。
