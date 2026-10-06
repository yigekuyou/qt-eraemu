# FLOWINPUTS

- **类别**：式中函数（EE 扩展，`#region EE_SystemInput拡張` / `#region EEで追加されたやつ`）
- **签名**：int FLOWINPUTS(int 字符串输入{, str 默认值})
- **文档来源**：两套中文文档（`ecd/`、`_extracted/zh/`）与 EM/EE readme 均未收录本函数名；系统输入扩展的语义与字段依据源码（`Runtime/Script/Process.SystemProc.cs`）。相关姊妹函数 `FLOWINPUT`（同区，`Runtime/Script/Statements/Function/Creator.cs:335`）不在本批清单内

## 语义

设置**系统输入等待**（标题画面选择、存档/读档编号、商店/调教等由引擎自动发起的输入等待）的输入形态与默认值：

- 第 1 参数非 0 → 今后的系统输入按**字符串输入**处理（输入结果写入 `RESULTS`）；为 0 → 整数输入（结果写入 `RESULT`）。
- 第 2 参数（可省）→ 字符串输入的**默认值**（`Process.flowinputDefString`）。
- 返回值恒为 `0`（不是设置结果）。
- 作用域与生命周期：这两个字段是**当前 Process（本次脚本调用）的实例字段**（`Runtime/Script/Process.SystemProc.cs:108-113`），源码中没有复位逻辑；因此设置会在本次脚本执行期间一直有效，直到脚本结束（Process 销毁）。
- 与 `FLOWINPUT`（同区姊妹函数）的配合关系（源码 `setWaitInput`，`Runtime/Script/Process.SystemProc.cs:116-149`）：
  - `FLOWINPUT` 负责「有没有默认值 / 能否鼠标 / 能否跳过 / 强制跳过」；
  - **字符串默认值只在 `flowinput == true`（即调用了 `FLOWINPUT` 且其第 2 参数非 0）时才被真正使用**：`if (flowinput) { req.HasDefValue = true; ... req.DefStrValue = flowinputDefString; }`——只调 `FLOWINPUTS` 不调 `FLOWINPUT` 时，输入类型会变成字符串，但默认值不会被套用（推定：由该分支结构直接得出）。
  - 强制跳过/可跳过发生时，字符串模式下把默认值写进 `RESULTS`（`Runtime/Script/Process.SystemProc.cs:137-144`）。

## 用法

### int FLOWINPUTS(int 字符串输入{, str 默认值})
- 字符串输入：非 0 → 之后的系统输入按字符串处理；0 → 按整数处理。
- 默认值：可选字符串，字符串输入时的初始值。
- 返回值：恒 `0`。
```erb
; 让之后的系统输入变成字符串输入，默认值 "abc"
FLOWINPUTS 1, "abc"
; 若同时希望该默认值真正生效，需要 FLOWINPUT 打开默认值开关（同区姊妹函数）：
; FLOWINPUT 0, 1      ; 第 2 参数非 0 → flowinput = true

; 恢复整数输入
FLOWINPUTS 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:336`（`["FLOWINPUTS"] = new FlowInputsMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7446`（`FlowInputsMethod`）
- 状态字段与消费点：`Runtime/Script/Process.SystemProc.cs:108-113`（字段）、`:116-149`（`setWaitInput`）
- 姊妹函数 `FLOWINPUT`：`Runtime/Script/Statements/Function/Creator.Method.cs:7423`（`FlowInputMethod`），注册 `Runtime/Script/Statements/Function/Creator.cs:335`

```text
构造（Creator.Method.cs:7448-7455）:
    返回类型 = long
    argumentTypeArrayEx = [{ Int, String }, OmitStart = 1]
    ; 第 1 参必填（整数）；第 2 参（默认值字符串）可省
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:7456-7463）:
    exm.Process.flowinputString = (args[0].GetIntValue(exm) != 0)
    if args.Count > 1:
        exm.Process.flowinputDefString = args[1].GetStrValue(exm)
    return 0

消费点 setWaitInput（Process.SystemProc.cs:116-149）:
    req = new InputRequest()
    if flowinput:                        ; 仅 FLOWINPUT 打开时
        req.HasDefValue = true
        req.DefIntValue = flowinputDef      ; FLOWINPUT 的默认整数值
        req.MouseInput  = flowinput
        req.DefStrValue = flowinputDefString ; ← FLOWINPUTS 设的字符串默认值在此被使用
    if flowinputString: req.InputType = InputType.StrValue
    else:               req.InputType = InputType.IntValue
    req.IsSystemInput = true
    if flowinputForceSkip:      systemResult = req.DefIntValue
                                if flowinputString: RESULTS = req.DefStrValue
    elif flowinputCanSkip && 控制台处于消息跳过状态:
                                同上
    console.WaitInput(req)
```

## 备注

- 本函数与 `FLOWINPUT` 都属「EE_SystemInput拡張」，但源码区域的注释写的是 EE 扩展、注册位置在 `#region EEで追加されたやつ`；两套中文文档与 EM/EE readme 都没有它们（EE readme 有「INPUT, INPUTS…に第二引数追加」条目，但那是 `INPUT` 系指令的扩展，与此处的系统输入是两件事）。
- 「系统输入」指引擎自身发起的等待（标题菜单、存档/读档选号、商店/调教等 `SystemStateCode.*_WaitInput` 路径，`Runtime/Script/Process.SystemProc.cs:236, 424, 602, 729, 895, 954, 990, 1003, 1024`），**不是** ERB 指令 `INPUT`/`INPUTS`——`INPUT` 指令有自己的实现，不读这两个字段（可 grep 验证：`flowinput` 只出现在 `Runtime/Script/Statements/Function/Creator.Method.cs` 与 `Runtime/Script/Process.SystemProc.cs`）。
- 只设 `FLOWINPUTS 1` 时输入类型确实变字符串，但默认值不生效（需要同时 `FLOWINPUT` 打开默认值开关）——这是最容易踩的坑，源码结构决定的（推定）。
- 返回值恒 0，别用它判断设置是否成功。
- 作为**语句**调用（如示例）也合法：所有式中函数都被登记为 `METHOD_SAFE | EXTENDED` 的指令，语句形态下返回值写入 `RESULT`，见 `Runtime/Script/Statements/Instraction.Child.cs:579-598`（`METHOD_Instruction`）、`Runtime/Script/Statements/FunctionIdentifier.cs:458`（所有式中函数注册为 `methodInstruction`，其 flag 于 `Runtime/Script/Statements/Instraction.Child.cs:584` 为 `METHOD_SAFE | EXTENDED`）。
- 本仓库移植版（`src/eraengine/`）未实现本函数。
