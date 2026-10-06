# BINPUTS

- **类别**：EE 扩展命令
- **签名**：
  - `BINPUTS`
  - `BINPUTS <默认值（FORM 字符串）>`
  - `BINPUTS <默认值（FORM 字符串）>, <把鼠标点击视为 Enter>`
  - `BINPUTS <默认值（FORM 字符串）>, <把鼠标点击视为 Enter>, <可被右键跳过>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・BINPUT、BINPUTS」；`EmueraEE_changelog.txt`（v31 追加、v35fix 修正）。ecd 套件与 zh 套件均未收录本命令。

## 语义

BINPUT 的字符串版：只接受「当前画面上已按钮化的值」的 INPUTS（接受的是用 `PRINTBUTTON "...", "字符串"` 形式做出来的字符串按钮）。键盘输入仍然可用，但画面上没有任何按钮对应的字符串会被弹掉。

- 执行时若打印缓冲区中还有未换行的内容，先强制换行并刷新画面。
- 若画面上一个按钮都没有：给了默认值则不等待输入，直接把默认值代入 `RESULTS`；连默认值都没有则抛 CodeEE。
- 「把鼠标点击视为 Enter」与「可被右键跳过」参数沿用 EM 版 INPUTS 扩展规则：右键跳过生效时，「视为 Enter」参数为 0 则默认值代入 `RESULTS:0`，非 0 则代入 `RESULTS:1`。
- 第 1 参数与 INPUTS 一样按 FORM 字符串解析（可含 `%表达式%`、`()` 需转义）。
- 输入结果代入 `RESULTS`（字符串）。

## 用法

### `BINPUTS`
```erb
PRINTL 请输入名字对应的按钮。
PRINTBUTTON "[ほげほげ] ", "ほげほげ"
PRINTBUTTON "[ぷげぷげ] ", "ぷげぷげ"
BINPUTS
PRINTL 输入了%RESULTS%。
```
### `BINPUTS <默认值>{, <鼠标点击视为 Enter>{, <可被右键跳过>}}`
```erb
PRINTBUTTON "[A] 攻撃", "A"
PRINTBUTTON "[B] 防御", "B"
BINPUTS "A"
;若画面上一个按钮都没有，不等待输入，RESULTS = "A"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:382`（枚举 `BINPUTS`）；`Runtime/Script/Statements/FunctionIdentifier.cs:428`（`addFunction(FunctionCode.BINPUTS, new BINPUTS_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2318`（`BINPUTS_Instruction`，`#region EE_BINPUT`）；参数解析 `Runtime/Script/Statements/ArgumentBuilder.cs:1245`（`SP_INPUTS_ArgumentBuilder`：第 1 参数为 FORM 字符串，第 2、3 参数须为整数，多出的参数告警并忽略）→ `Runtime/Script/Statements/Argument.cs:12`（`SpInputsArgument`）；输入请求类型 `Runtime/InputRequest.cs:13`（`InputType.StrButton`）

```text
构造: ArgBuilder = SP_INPUTS（参数：FORM 字符串 Def, 整数 Mouse, 整数 CanSkip）
      flag = IS_PRINT | IS_INPUT

DoInstruction:
    if 打印缓冲区非空: 强制换行
    Console.RefreshStrings(true)
    req.InputType = StrButton
    if Def != null:
        req.HasDefValue = true
        req.DefStrValue = Def.GetStrValue(exm)
    if Mouse != null:
        req.MouseInput = (Mouse.GetIntValue(exm) != 0)
    Window.ApplyTextBoxChanges()
    if CanSkip != null and Console.MesSkip(右键跳过中):
        if Mouse == 0 或 Mouse 值为 0: RESULTS(=RESULTS:0) = Def 字符串
        else:                          RESULTS:1          = Def 字符串
        return
    count = 0
    for line in DisplayLineList 逆序:
        for button in line.Buttons:
            if button.Generation != 0 且 != LastButtonGeneration:
                跳出整个扫描
            else if button.IsButton:     # 字符串版不要求 IsInteger
                count++
    for part in Console.EscapedParts:
        if part is ConsoleDivPart:
            for line in part.Children 逆序:
                for button in line.Buttons:
                    if button.IsButton:
                        count++; 跳出全部扫描
    if count == 0:
        if Def == null: throw CodeEE(「BINPUTS 可选择的按钮一个也没有」)
        else: RESULTS = Def 字符串; return
    Console.WaitInput(req)               # 结果 → RESULTS
```

## 备注

- 与 BINPUT 的实现差异只有三点：`InputType` 为 `StrButton` 而非 `IntButton`；默认值为字符串（FORM 解析）；按钮扫描不要求 `button.IsInteger`。
- ecd 与 zh 两套文档均未收录本命令。
- 变更史：v31 追加；v35fix「BINPUTS の不具合を修正」（细节未记载）。
