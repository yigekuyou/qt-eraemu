# CLEARTEXTBOX

- **类别**：命令
- **签名**：`CLEARTEXTBOX`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md`「未整理项目」下的「CLEARTEXTBOX」小节；`Era-Chinese-Documentation` 未收录本命令

## 语义

清空最下方输入栏（文本框）中的全部文本。不影响游戏主画面的打印内容，也不产生换行等输出副作用。无参数、无返回值。

## 用法

### `CLEARTEXTBOX`
- 无参数。
```erb
PRINTL 请重新输入
CLEARTEXTBOX   ; 清空输入栏残留文本
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:288` → `argb[FunctionArgType.VOID]`, METHOD_SAFE | EXTENDED
- 实现：`Runtime/Script/Process.ScriptProc.cs:753`（switch-case，`Process.ScriptProc.DoScriptProcMethod` 内）

```text
case FunctionCode.CLEARTEXTBOX:
    console.ClearText()
    break

ClearText():                                   // UI/Game/EmueraConsole.Print.cs:645
    window.clear_richText()                    // 直接清空输入窗口的富文本内容
```

## 备注

- ecd 文档将其归入"未整理项目"，说明该命令的官方说明尚不完整；语义与源码一致（清空输入栏文本）。
- 它是少数通过 `Runtime/Script/Process.ScriptProc.cs` 的 switch 分发（而非独立指令类）实现的 VOID 命令。
- zh 文档套件未收录该命令。
