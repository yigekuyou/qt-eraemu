# SETCOLORBYNAME

- **类别**：命令
- **签名**：`SETCOLORBYNAME <文本>`
- **文档来源**：`ecd/docs/translation/Command.md`（SETCOLORBYNAME / SETBGCOLORBYNAME 小节）；`Era-Chinese-Documentation/docs/` 未收录该命令小节

## 语义

通过预设的颜色名称来设置文字颜色。除参数是颜色名称外，与 `SETCOLOR` 相同：设置后一直生效，直到被覆盖或 `RESETCOLOR` 还原。当前文字颜色可用 `GETCOLOR` 获取。预设的颜色名称参见颜色枚举（.NET KnownColor，如 `"Red"`、`"Orange"`）。

参数必须是字符串常量。名称不是有效颜色名时报错；`"transparent"`（透明）不被支持，单独报错。

## 用法

### SETCOLORBYNAME <文本>

- `<文本>`：颜色名称字符串。

```erb
SETCOLORBYNAME "Orange"
PRINTL 橙色文字
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:272`（`argb[FunctionArgType.STR], METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:423`（switch-case `FunctionCode.SETCOLORBYNAME`）；最终落点 `UI/Game/EmueraConsole.Print.cs:92`（`SetStringStyle(Color)`）

```text
case SETCOLORBYNAME:
    colorName = func.Argument.ConstStr     // STR 参数类型：必须是字符串常量
    c = Color.FromName(colorName)          // 名称未知时返回 A=0 的透明色
    if c.A == 0:
        if str.Equals("transparent", 不区分大小写):
            throw CodeEE("不支持 transparent")
        throw CodeEE("颜色名 " + colorName + " 不正确")
    exm.Console.SetStringStyle(c)          // userStyle.Color = c; ColorChanged = c != 默认色
```

## 备注

- 与 `SETBGCOLORBYNAME` 逐行同构，唯一差别是最终调用 `SetStringStyle`（文字色）而非 `SetBgColor`（背景色）。
- 无效名称的判定依据是 `Color.FromName` 返回 `A == 0`；因此若某个合法名称本身 Alpha 为 0 也会被误判为无效，但 KnownColor 全集中不存在此类名称，实际无影响。
- `zh/` 文档套件未收录本命令，无从交叉核对。
