# BINPUT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令
- **签名**：
  - `BINPUT`
  - `BINPUT <默认值>`
  - `BINPUT <默认值>, <把鼠标点击视为 Enter>`
  - `BINPUT <默认值>, <把鼠标点击视为 Enter>, <可被右键跳过>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・BINPUT、BINPUTS」；`EmueraEE_changelog.txt`（v31 追加、v31fix/v35/v35fix 修正）。ecd 套件与 zh 套件均未收录本命令。

## 语义

只接受「当前画面上已按钮化的值」的 INPUT。注意它不是「只接受按钮输入」，而是「只接受已经按钮化显示出来的值」：玩家仍可用键盘输入，但凡是画面上没有对应按钮的数值都会被弹掉，从而在不牺牲键盘操作的前提下，杜绝只能靠鼠标点击才可能产生的非法值。

- 执行时若打印缓冲区中还有未换行的内容，会先强制换行并刷新画面（保证按钮判定覆盖最新一屏）。
- 若画面上一个整数按钮都没有：给了默认值则不等待输入，直接把默认值代入 `RESULT`；连默认值都没有则抛 CodeEE（游戏无法继续推进）。
- 「把鼠标点击视为 Enter」与「可被右键跳过」两个参数沿用 EM 版 INPUT 扩展的规则：右键跳过生效时，若「视为 Enter」参数为 0，默认值代入 `RESULT:0`；非 0 则代入 `RESULT:1`。
- 输入结果代入 `RESULT`（整数）。

## 用法

### `BINPUT`
无默认值。画面上必须至少有一个整数按钮，否则报错。
```erb
PRINTL 请选择。
PRINTBUTTON "[0] はい", 0
PRINTBUTTON "[1] いいえ", 1
BINPUT
IF RESULT == 0
    PRINTL 选择了是。
ELSE
    PRINTL 选择了否。
ENDIF
```
### `BINPUT <默认值>{, <鼠标点击视为 Enter>{, <可被右键跳过>}}`
```erb
PRINTBUTTON "[0] 返回", 0
PRINTBUTTON "[1] 继续", 1
BINPUT 0
;若画面上一个按钮都没有，不等待输入，RESULT = 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:381`（枚举 `BINPUT`）；`Runtime/Script/Statements/FunctionIdentifier.cs:427`（`addFunction(FunctionCode.BINPUT, new BINPUT_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2228`（`BINPUT_Instruction`，`#region EE_BINPUT`）；参数解析 `Runtime/Script/Statements/ArgumentBuilder.cs:2273`（`SP_INPUT_ArgumentBuilder`）→ `Runtime/Script/Statements/Argument.cs:12`（`SpInputsArgument`：Def/Mouse/CanSkip 三项）；输入请求类型 `Runtime/InputRequest.cs:13`（`InputType.IntButton`）

```text
构造: ArgBuilder = SP_INPUT（0~3 个整数参数：Def, Mouse, CanSkip）
      flag = IS_PRINT | IS_INPUT

DoInstruction:
    if 打印缓冲区非空: 强制换行          # 保证「PRINT 后不换行就 BINPUT」也能识别该行按钮
    Console.RefreshStrings(true)         # 刷新显示串
    req.InputType = IntButton
    if Def != null:
        req.HasDefValue = true
        req.DefIntValue = Def.GetIntValue(exm)
    if Mouse != null:
        req.MouseInput = (Mouse.GetIntValue(exm) != 0)
    Window.ApplyTextBoxChanges()
    if CanSkip != null and Console.MesSkip(右键跳过中):
        if Mouse == 0 或 Mouse 值为 0: RESULT(=RESULT:0) = Def 值
        else:                          RESULT:1       = Def 值
        return                         # 跳过状态下不等待输入
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
                    if button.IsButton 且 button.IsInteger:
                        count++; 跳出全部扫描
    if count == 0:
        if Def == null: throw CodeEE(「BINPUT 可选择的按钮一个也没有」)
        else: RESULT = Def 值; return    # 不等待输入
    Console.WaitInput(req)               # 正常等待，只接受按钮化数值，结果 → RESULT
```

## 备注

- ecd（Emuera em 文档站翻译）与 zh（Era-Chinese-Documentation）两套文档均未收录本命令，语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准，两者一致。
- README 中「ボタンが一つも無い状態で実行された場合は…エラーになる」对应源码 `trerror.NothingButtonBinput`（`Runtime/Script/Statements/Instraction.Child.cs:2308`）。
- 变更史（EmueraEE_changelog.txt）：v31 追加 BINPUT/BINPUTS；v31fix「按钮一个都没有时返回默认值、无默认值才报错」「与 div 功能合用时失效的修复」；v35「PRINT 后不换行执行 BINPUT 时该行按钮不被识别的修复」「多余换行的修复」；v35fix「BINPUTS 不具合修正」。这些行为均已体现在当前源码中（换行处理、div 扫描、count==0 分支）。
- 本仓库另有同族的 `ONEBINPUT`/`ONEBINPUTS`（单字符限定版，`Runtime/Script/Statements/Instraction.Child.cs:2424` 起），不在本批范围。
