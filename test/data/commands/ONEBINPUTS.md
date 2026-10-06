# ONEBINPUTS

- **类别**：EE 扩展命令（Emuera 枚举成员；EE 扩展；两套中文文档未收录，语义据源码推定）
- **签名**（与 `BINPUTS` 共用同一参数构建器 `SP_INPUTS`，推定自 `Runtime/Script/Statements/ArgumentBuilder.cs:1245`）：
  - `ONEBINPUTS`
  - `ONEBINPUTS <默认值（FORM 字符串）>`
  - `ONEBINPUTS <默认值（FORM 字符串）>, <把鼠标点击视为 Enter>`
  - `ONEBINPUTS <默认值（FORM 字符串）>, <把鼠标点击视为 Enter>, <可被右键跳过>`
- **文档来源**：无任何文档收录。`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt` 与本目录 `EmueraEE_changelog.txt`（v31「BINPUT及びBINPUTS命令追加」等）均**未出现 ONEBINPUTS 字样**；两套中文文档亦未收录。语义据源码推定：实现与 `BINPUTS` 逐行相同，唯一差异是给输入请求附加 `OneInput = true`。

## 语义

`BINPUTS` 的单字符限定版：与 `BINPUTS` 一样「只接受当前画面上已按钮化的字符串值」（对应 `PRINTBUTTON "...", "字符串"` 做出来的按钮），**额外**把输入请求标记为 `OneInput`（单次输入）。

- 参数、按钮扫描、默认值回退、错误行为与 `BINPUTS` 完全一致：执行时先强制换行并刷新画面；画面上一个按钮都没有时，有默认值则不等待输入直接代入 `RESULTS`，没有默认值则抛 CodeEE（错误文案把命令名填成 `ONEBINPUTS`，见 `Runtime/Utils/EvilMask/Lang.cs:1192`）。
- 第 1 参数按 FORM 字符串解析（可含 `%表达式%`，`()` 需转义），与 `INPUTS`/`BINPUTS` 相同。
- `OneInput = true` 的效果在控制台层（源码事实，与 `ONEBINPUT` 完全相同）：
  1. **输入截断为首字符**：键盘/粘贴输入超过 1 个字符时只取第一个字符（例外：鼠标点击产生输入且 CONFIG 允许长鼠标输入时为整体输入）——`UI/Game/EmueraConsole.cs:1268`；
  2. **单字符自动提交**：`IsWaintingOnePhrase`（`:371-376`）为真时，文本框内容一旦增加就立即当作回车提交——`UI/Framework/Forms/MainWindow.cs:1124-1152`；鼠标点击按钮时文本框内容按「替换」而非「追加」处理（`UI/Framework/Forms/MainWindow.cs:698`、`:733`）；
  3. **禁用 `@` 系统命令**：`:1242`；
  4. 带时限时调用 `window.update_lastinput()`（`:647-648`）。
- 输入结果代入 `RESULTS`（字符串）；配合第 2、3 参数时沿用 EM 版 INPUTS 扩展规则：右键跳过生效时，「视为 Enter」参数为 0 则默认值代入 `RESULTS:0`，非 0 则代入 `RESULTS:1`。
- flag 为 `IS_PRINT | IS_INPUT`（**不含** `EXTENDED`，与 `BINPUTS` 相同；注意 `ONEINPUTS` 是带 `EXTENDED` 的）。

## 用法

### `ONEBINPUTS`
```erb
PRINTL 请点一个字符串按钮，或敲入首字符后自动提交。
PRINTBUTTON "[ほげほげ] ", "ほげほげ"
PRINTBUTTON "[ぷげぷげ] ", "ぷげぷげ"
ONEBINPUTS
PRINTL 输入了%RESULTS%。
```
此处若用键盘敲入任意字符串，只会保留首字符，因此实际能命中的只有首字符与按钮值一致的那些按钮（这正是「ONE」限定的用意）。

### `ONEBINPUTS <默认值>{, <鼠标点击视为 Enter>{, <可被右键跳过>}}`
```erb
PRINTBUTTON "[A] 攻撃", "A"
ONEBINPUTS "A"
;若画面上一个按钮都没有，不等待输入，RESULTS = "A"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:384`（枚举 `ONEBINPUTS`，`#region EE`）；`Runtime/Script/Statements/FunctionIdentifier.cs:430`（`addFunction(FunctionCode.ONEBINPUTS, new ONEBINPUTS_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2514`（`ONEBINPUTS_Instruction`，`#region EE_ONEBINPUT` 自 `:2423`）；参数解析 `Runtime/Script/Statements/ArgumentBuilder.cs:1245`（`SP_INPUTS_ArgumentBuilder`：第 1 参数为 FORM 字符串，第 2、3 参数须为整数，多出的参数告警并忽略）→ `Runtime/Script/Statements/Argument.cs:12`（`SpInputsArgument`）；输入请求类型 `Runtime/InputRequest.cs:15`（`InputType.StrButton`）+ `:50`（`OneInput`）

```text
构造: ArgBuilder = SP_INPUTS（同 BINPUTS）
      flag = IS_PRINT | IS_INPUT          # 与 BINPUTS 相同，未加 EXTENDED

DoInstruction（与 BINPUTS_Instruction 逐行相同，仅输入类型与 OneInput 不同）:
    if 打印缓冲区非空: 强制换行
    Console.RefreshStrings(true)
    req = InputRequest{ InputType = StrButton, OneInput = true }   # ← 与 BINPUTS 的唯一实质差异
    if Def != null:   req.HasDefValue = true; req.DefStrValue = Def.GetStrValue(exm)
    if Mouse != null: req.MouseInput = (Mouse.GetIntValue(exm) != 0)
    Window.ApplyTextBoxChanges()
    if CanSkip != null and Console.MesSkip(右键跳过中):
        if Mouse 值为 0: RESULTS(=RESULTS:0) = Def 字符串
        else:            RESULTS:1          = Def 字符串
        return
    count = 0
    for line in DisplayLineList 的逆序:
        for button in line.Buttons:
            if button.Generation != 0 且 != LastButtonGeneration:
                跳出整个扫描
            else if button.IsButton:        # 字符串版不要求 IsInteger
                count++
    for part in Console.EscapedParts:       # div 内部也要扫
        if part is ConsoleDivPart:
            for line in part.Children 逆序:
                for button in line.Buttons:
                    if button.IsButton: count++; 跳出全部扫描
    if count == 0:
        if Def == null: throw CodeEE("…{0}…", "ONEBINPUTS")   # trerror.NothingButtonBinput
        else:           RESULTS = Def 字符串; return           # 不等待输入
    Console.WaitInput(req)               # 等待单字符输入；结果 → RESULTS
```

控制台侧 `OneInput` 的消费点与 `ONEBINPUT` 相同：`UI/Game/EmueraConsole.cs:1268`（截断首字符）、`:1242`（禁用 `@` 系统命令）、`:371`（`IsWaintingOnePhrase`，被 `UI/Framework/Forms/MainWindow.cs:1124-1152`/`:698`/`:733` 使用）、`:647-648`（有时限时 `update_lastinput`）。

## 备注

- 两套中文文档均未收录本命令；EE readme/changelog 亦未提到它（只有 BINPUT/BINPUTS 的 v31 记录），因此**语义据源码推定**。
- 与 `BINPUTS` 的差异（源码级，仅此三点）：
  1. `InputRequest.OneInput = true`；
  2. 错误文案的命令名参数为 `"ONEBINPUTS"`（`Runtime/Script/Statements/Instraction.Child.cs:2608`）；
  3. 类名/注册枚举不同（`ONEBINPUTS_Instruction`）。
- 与 `ONEBINPUT` 的差异：`InputType` 为 `StrButton`（默认值/结果走字符串 `RESULTS`），按钮扫描不要求 `button.IsInteger`——与 `BINPUT`/`BINPUTS` 之间的差异一一对应。
- 「ONE」前缀的含义以源码为准：**不是**「只接受一个按钮」，而是沿用 `ONEINPUT`/`ONEINPUTS` 一族的「单次/单字符输入」语义。与 `ONEBINPUT.md`、`BINPUTS.md`、`ONEINPUTS.md` 对照阅读。
- 与 `BREAKBUTTON` 的关系：按钮扫描只认 `LastButtonGeneration` 一代，`BREAKBUTTON` 会使当前一代按钮全部作废，从而让本命令视为「无按钮」而走默认值/报错（见 `BREAKBUTTON.md`）。
