# GETTEXTBOX

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（式中函数）
- **签名**：
  - `GETTEXTBOX`
- **文档来源**：EM+EE 在线文档「输入框相关命令」（TEXTBOX 页，与 `SETTEXTBOX`/`MOVETEXTBOX`/`RESUMETEXTBOX` 同页）；`EmueraEE_readme.txt`（该条目被误写作 `GETTEXTSIZE`，见备注）；`EmueraEE_changelog.txt` v21「GETTEXTBOX,SETTEXTBOX追加」；`ecd/Command.md` 未收录；zh 套件未收录。

## 语义

返回执行时输入框（文本框）中当前已输入的字符串。可用于在 INPUT/INPUTS 等待过程中实时读取玩家正在键入的内容。无参数、无副作用，返回字符串型。

## 用法

### `GETTEXTBOX`
- 无参数；返回执行时输入框内的字符串。
```erb
;配合 INPUT 读取等待期间输入框内的实时内容
INPUT
PRINTFORMW 刚才输入框内的内容为 %GETTEXTBOX%
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:327`（`["GETTEXTBOX"] = new GetTextBoxMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7318`（`GetTextBoxMethod`，`#region EE_textbox拡張`）

```text
class GetTextBoxMethod : FunctionMethod
    构造: ReturnType = string; argumentTypeArray = []; CanRestructure = false
    GetStrValue(exm, arguments):
        返回 exm.Console.Window.TextBox.Text   # 直接读取主窗口输入框控件的文本
```

## 备注

- EE v21 加入。EE 日文 readme 第 160 行将本命令误标为 `GETTEXTSIZE`（描述文字「実行時点でテキストボックスに入力されている内容を取得する」实为本命令语义）；同文件 changelog v21 与 EM+EE 在线文档均作 `GETTEXTBOX`，应以 GETTEXTBOX 为准。
- 与本命令同页的 `SETTEXTBOX`（替换输入框内容）在本仓库亦有实现（`ChangeTextBoxMethod`，同 region）。
- 在线文档注明「命令/行内函数两种写法均有效」，本仓库仅实现式中函数形式。
