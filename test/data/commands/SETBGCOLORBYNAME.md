# SETBGCOLORBYNAME

- **类别**：命令
- **签名**：`SETBGCOLORBYNAME <文本>`
- **文档来源**：`ecd/docs/translation/Command.md`（SETCOLORBYNAME / SETBGCOLORBYNAME 小节）；`Era-Chinese-Documentation/docs/` 未收录该命令小节

## 语义

通过预设的颜色名称来设置文字显示的背景色。除参数是颜色名称外，与 `SETBGCOLOR` 相同：设置后一直生效，直到下一次变更或 `RESETBGCOLOR`。预设颜色名称即 .NET 的 `KnownColor` 枚举（如 `"Red"`、`"DarkBlue"`）。

参数必须是字符串常量。名称不是有效颜色名时报错；`"transparent"`（透明）不被支持，单独报错。

## 用法

### SETBGCOLORBYNAME <文本>

- `<文本>`：颜色名称字符串。

```erb
SETBGCOLORBYNAME "DarkBlue"
PRINTL 深蓝底文字
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:276`（`argb[FunctionArgType.STR], METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:470`（switch-case `FunctionCode.SETBGCOLORBYNAME`）；最终落点 `UI/Game/EmueraConsole.Print.cs:111`（`SetBgColor`，防闪烁节流见 SETBGCOLOR 文档）

```text
case SETBGCOLORBYNAME:
    colorName = func.Argument.ConstStr     // STR 参数类型：必须是字符串常量
    c = Color.FromName(colorName)          // 名称未知时返回 A=0 的透明色
    if c.A == 0:
        if str.Equals("transparent", 不区分大小写):
            throw CodeEE("不支持 transparent")
        throw CodeEE("颜色名 " + colorName + " 不正确")
    exm.Console.SetBgColor(c)
```

## 备注

- `Color.FromName` 对未知名称返回 `A == 0` 的颜色，源码以此判断名称无效；因此文档中的「预设的颜色名称」实为 .NET KnownColor 全集，而不是 Emuera 自定义的颜色表。
- 参数类型是 `STR`（要求常量字符串），与 `SETCOLORBYNAME` 完全一致；不能用变量或拼接表达式传入名称。
- `zh/` 文档套件未收录本命令，无从交叉核对。
