# RESETBGCOLOR

- **类别**：命令
- **签名**：RESETBGCOLOR
- **文档来源**：`ecd/docs/translation/Command.md`（SETBGCOLOR/RESETBGCOLOR 小节）；Era-Chinese-Documentation 未单独收录该命令

## 语义

将背景色恢复为默认背景色（配置项 `Config.BackColor`）。基本作用与 `SETCOLOR` / `RESETCOLOR` 类似，只是作用对象是背景色。出于安全原因，背景色变更后的 0.2 秒内若再次出现变更背景色的指令，会被强制等待 0.2 秒后再执行。当前背景色可用 `GETBGCOLOR` 获取，默认背景色可用 `GETDEFBGCOLOR` 获取。

## 用法

### RESETBGCOLOR
无参数。

```erb
SETBGCOLOR 0, 0, 64
PRINTL 深蓝背景。
RESETBGCOLOR
PRINTL 默认背景。
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:277` → `new RESETBGCOLOR_Instruction()`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:177`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1548`（`RESETBGCOLOR_Instruction`）

```text
类 RESETBGCOLOR_Instruction:
  构造: 参数构造器 = VOID（无参数）; 标志 = METHOD_SAFE | EXTENDED

  DoInstruction(exm, func, state):
    exm.Console.SetBgColor(Config.BackColor)   // 把背景色设回配置的默认背景色
    //（0.2 秒防抖限制在控制台 SetBgColor 内部实现，不在此指令层）
```

## 备注

- 文档所述「0.2 秒内再次变更会被强制等待」的防抖行为是在控制台层（`SetBgColor` 的实现）处理的，指令类本身只是转调。
- Era-Chinese-Documentation 套件未收录本命令。
