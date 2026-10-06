# GETDEFBGCOLOR

- **类别**：式中函数
- **签名**：int GETDEFBGCOLOR()
- **文档来源**：`ecd/Command.md`「### GETDEFBGCOLOR」；`ecd/Expression.md`（表达式内函数签名列表）

## 语义

返回"设置"中指定的默认背景颜色，数值格式为 `0xRRGGBB`。与 `GETCOLOR` 类似；区别在于它返回设置里的默认背景色，而不是 `SETBGCOLOR` 修改后的当前背景色（后者用 `GETBGCOLOR`）。

无参数。只能在表达式中使用。

## 用法

### int GETDEFBGCOLOR()
- 无参数。
- 返回值：设置中指定的默认背景颜色，`0xRRGGBB` 格式的整数。
```erb
PRINTL 默认背景色为 0x{GETDEFBGCOLOR(),6:X}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:47`（`["GETDEFBGCOLOR"] = new GetBGColorMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2604`（`GetBGColorMethod`，与 `GETBGCOLOR` 共用同一个类）

```text
构造（isDef = true）：返回类型 = long；参数 = []；CanRestructure = true。

GetIntValue(exm, args):
    color ← Config.BackColor            ; isDef = true：取配置默认背景色
    返回 color.ToArgb() & 0xFFFFFF      ; 0xRRGGBB
```

## 备注

- ecd/Command.md 以「将设置指定的默认背景颜色返回到 `RESULT:0` 中」描述；实际注册形态是式中函数。
- 与 `GETBGCOLOR`（注册于 `Runtime/Script/Statements/Function/Creator.cs:47`，`isDef = false`，取 `Console.bgColor`）共用 `GetBGColorMethod`。
- zh 套件（Era-Chinese-Documentation）未收录本函数。
