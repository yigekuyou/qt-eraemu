# ONEBINPUT

- **类别**：EE 扩展命令（Emuera 枚举成员；EE 扩展；两套中文文档未收录，语义据源码推定）
- **签名**（与 `BINPUT` 共用同一参数构建器 `SP_INPUT`，推定自 `Runtime/Script/Statements/ArgumentBuilder.cs:2273`）：
  - `ONEBINPUT`
  - `ONEBINPUT <默认值>`
  - `ONEBINPUT <默认值>, <把鼠标点击视为 Enter>`
  - `ONEBINPUT <默认值>, <把鼠标点击视为 Enter>, <可被右键跳过>`
- **文档来源**：无任何文档收录。`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt` 与本目录 `EmueraEE_changelog.txt`（v31「BINPUT及びBINPUTS命令追加」、v41「TOOLTIP_IMG追加」等）均**未出现 ONEBINPUT 字样**；两套中文文档亦未收录。语义据源码推定：实现与 `BINPUT` 逐行相同，唯一差异是给输入请求附加 `OneInput = true`。

## 语义

`BINPUT` 的单字符限定版：与 `BINPUT` 一样「只接受当前画面上已按钮化的整数值」，**额外**把输入请求标记为 `OneInput`（单次输入）。

- 参数、按钮扫描、默认值回退、错误行为与 `BINPUT` 完全一致：执行时先强制换行并刷新画面；画面上一个整数按钮都没有时，有默认值则不等待输入直接代入 `RESULT`，没有默认值则抛 CodeEE（错误文案把命令名填成 `ONEBINPUT`，见 `Runtime/Utils/EvilMask/Lang.cs:1192`）。
- `OneInput = true` 的效果在控制台层（源码事实）：
  1. **输入截断为首字符**：键盘/粘贴输入超过 1 个字符时只取第一个字符（例外：鼠标点击产生输入且 CONFIG 允许长鼠标输入时为整体输入）——`UI/Game/EmueraConsole.cs:1268`；
  2. **单字符自动提交**：`IsWaintingOnePhrase`（`:371-376`）为真时，文本框内容一旦增加就立即当作回车提交，无需按回车——`UI/Framework/Forms/MainWindow.cs:1124-1152`；鼠标点击按钮时文本框内容按「替换」而非「追加」处理（`UI/Framework/Forms/MainWindow.cs:698`、`:733`）；
  3. **禁用 `@` 系统命令**：以 `@` 开头的输入不再被解释为系统命令（`:1242`）；
  4. 带时限时调用 `window.update_lastinput()`（`:647-648`）。
- 输入结果代入 `RESULT`（整数）；配合第 2、3 参数时沿用 EM 版 INPUT 扩展规则：右键跳过生效时，「视为 Enter」参数为 0 则默认值代入 `RESULT:0`，非 0 则代入 `RESULT:1`。
- flag 为 `IS_PRINT | IS_INPUT`（**不含** `EXTENDED`，与 `BINPUT` 相同；注意 `ONEINPUT`/`ONEINPUTS` 是带 `EXTENDED` 的）。

## 用法

### `ONEBINPUT`
```erb
PRINTL 请按一个数字按钮（也可直接敲一位数字，敲下即提交）。
PRINTBUTTON "[0] はい", 0
PRINTBUTTON "[1] いいえ", 1
ONEBINPUT
IF RESULT == 0
    PRINTL 选择了是。
ELSE
    PRINTL 选择了否。
ENDIF
```
### `ONEBINPUT <默认值>{, <鼠标点击视为 Enter>{, <可被右键跳过>}}`
```erb
PRINTBUTTON "[0] 返回", 0
ONEBINPUT 0
;若画面上一个按钮都没有，不等待输入，RESULT = 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:383`（枚举 `ONEBINPUT`，`#region EE`）；`Runtime/Script/Statements/FunctionIdentifier.cs:429`（`addFunction(FunctionCode.ONEBINPUT, new ONEBINPUT_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2424`（`ONEBINPUT_Instruction`，`#region EE_ONEBINPUT` 自 `:2423`）；参数解析 `Runtime/Script/Statements/ArgumentBuilder.cs:2273`（`SP_INPUT_ArgumentBuilder`：0~3 个整数参数 Def/Mouse/CanSkip，第 4 个起被静默忽略）→ `Runtime/Script/Statements/Argument.cs:12`（`SpInputsArgument`）；输入请求类型 `Runtime/InputRequest.cs:14`（`InputType.IntButton`）+ `:50`（`OneInput`）

```text
构造: ArgBuilder = SP_INPUT（同 BINPUT）
      flag = IS_PRINT | IS_INPUT          # 与 BINPUT 相同，未加 EXTENDED

DoInstruction（与 BINPUT_Instruction 逐行相同，仅输入类型与 OneInput 不同）:
    if 打印缓冲区非空: 强制换行
    Console.RefreshStrings(true)
    req = InputRequest{ InputType = IntButton, OneInput = true }   # ← 与 BINPUT 的唯一实质差异
    if Def != null:   req.HasDefValue = true; req.DefIntValue = Def.GetIntValue(exm)
    if Mouse != null: req.MouseInput = (Mouse.GetIntValue(exm) != 0)
    Window.ApplyTextBoxChanges()
    if CanSkip != null and Console.MesSkip(右键跳过中):
        if Mouse 值为 0: RESULT(=RESULT:0) = Def 值
        else:            RESULT:1          = Def 值
        return
    count = 0
    for line in DisplayLineList 的逆序:            # 从最后一行往前扫
        for button in line.Buttons:
            if button.Generation != 0 且 != LastButtonGeneration:
                跳出整个扫描（只认最新一代的按钮）
            else if button.IsButton 且 button.IsInteger:
                count++
    # div（HTML div 标签块）内部也要扫
    for part in Console.EscapedParts:
        if part is ConsoleDivPart:
            for line in part.Children 逆序:
                for button in line.Buttons:
                    if button.IsButton 且 button.IsInteger: count++; 跳出全部扫描
    if count == 0:
        if Def == null: throw CodeEE("…{0}…", "ONEBINPUT")   # trerror.NothingButtonBinput
        else:           RESULT = Def 值; return               # 不等待输入
    Console.WaitInput(req)               # 等待单字符输入；结果 → RESULT
```

控制台侧 `OneInput` 的消费点：`UI/Game/EmueraConsole.cs:1268`（截断首字符）、`:1242`（禁用 `@` 系统命令）、`:371`（`IsWaintingOnePhrase`，被 `UI/Framework/Forms/MainWindow.cs:1124-1152` 的自动提交逻辑与 `:698`/`:733` 的鼠标点击逻辑读取）、`:647-648`（有时限时 `update_lastinput`）。

## 备注

- 两套中文文档（ecd 与 Era-Chinese-Documentation）均未收录本命令；EE readme/changelog 亦未提到它（BINPUT/BINPUTS 有 v31 记录，ONEBINPUT 无），因此**语义据源码推定**——推定链条：`ONEBINPUT = BINPUT + OneInput 标志`，且与既有同族文档口径一致（`BINPUT.md:91` 已把它描述为「单字符限定版」）。
- 与 `BINPUT` 的差异（源码级，仅此三点）：
  1. `InputRequest.OneInput = true`；
  2. 错误文案的命令名参数为 `"ONEBINPUT"`（`Runtime/Script/Statements/Instraction.Child.cs:2504`）；
  3. 类名/注册枚举不同（`ONEBINPUT_Instruction`）。
  其余（`InputType.IntButton`、参数解析器、按钮扫描、默认值与 CanSkip 逻辑）完全相同。
- 「ONE」前缀的含义以源码为准：**不是**「只接受一个按钮」，而是沿用 `ONEINPUT`/`ONEINPUTS` 一族的「单次/单字符输入」语义（输入被截断为首字符、单字符即自动提交）。与 `ONEINPUT.md`、`ONEINPUTS.md`、`BINPUT.md` 对照阅读。
- 参数数量上限：`SP_INPUT_ArgumentBuilder` 自 `:2332-2349` 只取前 3 个参数，第 4 个起被静默忽略（与 `BINPUT` 同名同源行为）。
- 与 `BREAKBUTTON` 的关系：按钮扫描只认 `LastButtonGeneration` 一代，`BREAKBUTTON` 会使当前一代按钮全部作废，从而让本命令视为「无按钮」而走默认值/报错（见 `BREAKBUTTON.md`）。
