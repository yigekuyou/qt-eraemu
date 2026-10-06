# GETDEFCOLOR

- **类别**：式中函数
- **签名**：int GETDEFCOLOR()
- **文档来源**：`ecd/Command.md`「### GETDEFCOLOR」；`ecd/Expression.md`（表达式内函数签名列表）

## 语义

返回"设置"中指定的默认文字颜色（即 emuera.config 里配置、未用 `SETCOLOR` 覆盖前的颜色），数值格式为 `0xRRGGBB`。与 `GETCOLOR` 类似，区别在于 `GETCOLOR` 返回当前实际使用的文字色，而 `GETDEFCOLOR` 始终返回设置里的默认值。

无参数。只能在表达式中使用。

## 用法

### int GETDEFCOLOR()
- 无参数。
- 返回值：设置中指定的默认文字颜色，`0xRRGGBB` 格式的整数。
```erb
PRINTL 当前文字色：{GETCOLOR()}，默认文字色：{GETDEFCOLOR()}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:44`（`["GETDEFCOLOR"] = new GetColorMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2572`（`GetColorMethod`，与 `GETCOLOR` 共用同一个类）

```text
构造（isDef = true）：返回类型 = long；参数 = []；CanRestructure = true（默认色是静态设置，允许常量折叠）。

GetIntValue(exm, args):
    color ← Config.ForeColor            ; isDef = true：取配置默认色而非控制台当前色
    返回 color.ToArgb() & 0xFFFFFF      ; 0xRRGGBB
```

## 备注

- ecd/Command.md 以「将设置指定的默认文字颜色返回到 `RESULT:0` 中」描述；实际注册形态是式中函数。
- 与 `GETCOLOR` 共用 `GetColorMethod`，仅构造参数 `isDef` 不同：`isDef` 同时决定取值来源（`Config.ForeColor` vs 控制台当前色）以及 `CanRestructure`（默认色允许常量折叠，当前色不允许）。
- zh 套件（Era-Chinese-Documentation）未收录本函数。
