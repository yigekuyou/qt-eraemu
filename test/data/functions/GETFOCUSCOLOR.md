# GETFOCUSCOLOR

- **类别**：式中函数
- **签名**：int GETFOCUSCOLOR()
- **文档来源**：`ecd/Command.md`「### GETFOCUSCOLOR」；`ecd/Expression.md`（表达式内函数签名列表）

## 语义

返回"设置"中指定的被选中文字颜色（焦点色，即当前选中按钮/文字的高亮颜色），数值格式为 `0xRRGGBB`。与 `GETCOLOR` 类似。

无参数。只能在表达式中使用。

## 用法

### int GETFOCUSCOLOR()
- 无参数。
- 返回值：设置中指定的被选中文字颜色，`0xRRGGBB` 格式的整数。
```erb
PRINTL 选中文字颜色为 0x{GETFOCUSCOLOR(),6:X}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:45`（`["GETFOCUSCOLOR"] = new GetFocusColorMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2590`（`GetFocusColorMethod`）

```text
构造：返回类型 = long；参数 = []；CanRestructure = true（取静态配置，允许常量折叠）。

GetIntValue(exm, args):
    返回 Config.FocusColor.ToArgb() & 0xFFFFFF    ; 0xRRGGBB
```

## 备注

- ecd/Command.md 以「将设置指定的被选中文字颜色返回到 `RESULT:0` 中」描述；实际注册形态是式中函数。
- 与 `GETDEFCOLOR`/`GETDEFBGCOLOR` 一样直接读配置项，因此允许常量折叠；这与 `GETCOLOR`（读运行中的控制台状态、不可折叠）不同。
- zh 套件（Era-Chinese-Documentation）未收录本函数。
