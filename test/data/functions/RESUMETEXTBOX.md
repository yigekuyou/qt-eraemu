# RESUMETEXTBOX

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（EE 扩展 / 输入框布局）
- **签名**：`int RESUMETEXTBOX(int X偏移, int Y偏移, int 宽度)`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）、EE readme、EM readme 均未收录本函数；同族 `GETTEXTBOX`/`SETTEXTBOX` 的既有文档（`test/data/commands/GETTEXTBOX.md`）提到 EM+EE 在线文档的 TEXTBOX 页把 `MOVETEXTBOX`/`RESUMETEXTBOX` 与之同页收录，但该页不在本仓库提取材料中。本文语义据源码（`Runtime/Script/Statements/Function/Creator.Method.cs:1969` 的 `MoveTextBoxMethod`（`resume = true` 分支）与 `UI/Framework/Forms/MainWindow.cs:188` 的 `ResetTextBoxPos`）。

## 语义

把输入框（文本框）恢复为**记入 `textBoxInfo` 的当前位置/尺寸**，并把文本位置状态重置为「未变更」。它是 `MOVETEXTBOX` 的逆操作。

**三个参数虽然必须写，但被实现完全忽略**——注册时用的是同一个 `MoveTextBoxMethod`，只是构造参数 `resume = true`：

```csharp
if (resume) exm.Console.Window.ResetTextBoxPos();   // 不读 arguments
else exm.Console.Window.SetTextBoxPos(...);
```

因此 `RESUMETEXTBOX(0, 0, 0)`、`RESUMETEXTBOX(1, 2, 3)` 效果完全相同；参数的意义只是满足解析期的「恰好 3 个整数」检查。返回值恒为 `1`。

`ResetTextBoxPos` 内部调用 `SetTextBoxPos(textBoxInfo)` 后把状态置为 `Unchanged`，即「以当前已生效的位置」为准回滚（`textBoxInfo` 是先前 `ApplyTextBoxChanges` 提交过的值），并让滚动条逻辑（`TextBoxPosChanged`/`TextBoxPosScrolledBack`）回到「未变更」状态。

## 用法

### int RESUMETEXTBOX(X偏移, Y偏移, 宽度)
- 三个参数：必须写满 3 个整数，但**被忽略**（保留形式上的对称性，便于与 `MOVETEXTBOX` 成对书写）。
- 返回值：恒 `1`。
```erb
; 临时改变输入框布局，用完后还原
MOVETEXTBOX 20, 40, 600
SETTEXTBOX("请输入名字：")
INPUT
RESUMETEXTBOX 0, 0, 0      ; 参数被忽略，仅执行「恢复位置」
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:309`（`["RESUMETEXTBOX"] = new MoveTextBoxMethod(true)` — 与 `:308` 的 `MOVETEXTBOX` 共用实现类）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1969`（`MoveTextBoxMethod`，`resume` 分支在 `:1981`）→ `UI/Framework/Forms/MainWindow.cs:188`（`ResetTextBoxPos`）

```text
MoveTextBoxMethod（resume = true 时即 RESUMETEXTBOX）:
    构造(b = true):
        ReturnType = long
        argumentTypeArray = [long, long, long]      # 恰好 3 个整数参数（仅形式要求）
        resume = true
    GetIntValue(exm, arguments):
        # 注意：不读取 arguments 的任何元素
        若 resume:
            exm.Console.Window.ResetTextBoxPos()
        返回 1

MainWindow.ResetTextBoxPos()（MainWindow.cs:188）:
    SetTextBoxPos(textBoxInfo)          # textBoxInfo = 已生效的位置/尺寸（控件初始化时记录于 :93-95，
                                        #  由 ApplyTextBoxChanges 在提交 nextTextBoxInfo 时更新）
    textBoxState = TextBoxState.Unchanged
                                        # → TextBoxPosChanged / TextBoxPosScrolledBack 均变为 false
```

## 备注

- 语义据源码；提取材料中无任何文档描述该函数。**「参数被忽略」是读源码得到的结论**（`Runtime/Script/Statements/Function/Creator.Method.cs:1981-1985`），不是推定。
- 「恢复到哪里」取决于 `textBoxInfo` 的当前值：它初始为窗体载入时文本框的原始位置（`UI/Framework/Forms/MainWindow.cs:93-95`），此后被 `ApplyTextBoxChanges` 更新为「最后一次真正应用的新位置」。因此 `RESUMETEXTBOX` 是「回到上一次生效的位置 / 取消待应用的移动」，而不是绝对意义的「回到出厂默认」。
- 与 `MOVETEXTBOX` 相同，位置变更走的是「登记 → 择机应用」两步（`TextBoxState.WatingToChange` / `ScrollBack` / `Changed`），`ResetTextBoxPos` 之后滚动条相关判定会认为「无变更」，从而不会再把待应用的 `nextTextBoxInfo` 提交上去。
- 参数个数/类型不符（例如写成 `RESUMETEXTBOX` 或给它字符串）会在解析期报错，即使实现不使用它们。
- 函数形态而非命令形态：注册在 `methodList`，须写成 `RESUMETEXTBOX(0, 0, 0)`。
