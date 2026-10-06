# SETTEXTBOX

- **类别**：EE 扩展命令
- **签名**：
  - `SETTEXTBOX <字符串表达式>`（EE readme 描述为命令形式）
  - `SETTEXTBOX(<字符串表达式>)`（本仓库中作为式中函数实现）
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「概要」节（・SETTEXTBOX：テキストボックスを任意の文字列に置き換える；EmueraEE_changelog.txt:114「GETTEXTBOX,SETTEXTBOX追加」）。ecd 文档未收录；zh 套件未收录。

## 语义

把输入框（文本框）当前内容整体替换为指定的任意字符串。常与 `GETTEXTBOX`（读取输入框当前内容）配对使用，例如在输入等待前预填默认文字。不影响显示行（PRINT 输出），只作用于下方的输入框。

## 用法

### `SETTEXTBOX <字符串>`
- `<字符串>`：字符串表达式（字面量或含展开的字符串均可）。替换后输入框内容即为目标字符串，玩家可以继续编辑。
```erb
;把输入框预填为名字的第一个字
SETTEXTBOX GETTEXTBOX()
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:328`（`["SETTEXTBOX"] = new ChangeTextBoxMethod()`，注册在 methodList，即**式中函数**而非指令）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7331`（`ChangeTextBoxMethod`）；最终落到 `UI/Framework/Forms/MainWindow.cs:215`（`ChangeTextBox`，`#region EE_textbox拡張`）

```text
ChangeTextBoxMethod:
    构造:
        ReturnType = long
        argumentTypeArray = [string]        # 恰好 1 个字符串参数
        CanRestructure = false
    GetIntValue(exm, arguments):
        exm.Console.Window.ChangeTextBox(arguments[0].GetStrValue(exm))
        返回 1                              # 恒返回 1

MainWindow.ChangeTextBox(str):
    richTextBox1.Text = str                 # 直接整体替换输入框文本
```

## 备注

- **文档与源码的形态差异**：EE readme 把 SETTEXTBOX/GETTEXTBOX 描述为命令，本仓库（EM 合并版）把它实现为式中函数（methodList），需写成 `SETTEXTBOX("文字")` 的函数调用形式；EM+EE 发行版 exe 中两者兼有（关键字帮助按命令对待）。语义一致。
- 源码恒返回 1，没有失败分支；参数个数/类型不符会在解析期报错。
- 同类命令 `GETTEXTBOX`（读取输入框内容）也在 `Runtime/Script/Statements/Function/Creator.cs:328` 注册为式中函数。
