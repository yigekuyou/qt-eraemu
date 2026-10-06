# FLOWINPUT

- **类别**：EE 扩展命令（EE readme 所载为命令形态；本仓库中实现为式中函数）
- **签名**：
  - `FLOWINPUT <默认值>{, <把鼠标左键点击视为 Enter>{, <可被右键跳过>}}`（EE readme 所载命令形态）
  - `FLOWINPUT(<默认值>{, <把鼠标左键点击视为 Enter>{, <可被右键跳过>{, <强制跳过>}}})`（本仓库式中函数形态，第 4 参数为源码扩展）
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・FLOWINPUT デフォルト値(, 左クリックをEnterキーとみなすか, 右クリックでスキップ可能か)」。ecd 套件与 zh 套件均未收录本命令。

## 语义

为「系统流程中发生的 INPUT」预先设置选项。SHOP 流程（`SHOW_SHOP` 后系统代为等待输入）等场景中，INPUT 不是 ERB 脚本自己发出的，无法直接使用 EM 版 INPUT 的默认值／鼠标点击扩展参数；`FLOWINPUT` 让脚本在这些系统输入发生前设置好默认值、左键视为 Enter、右键可跳过等选项，下一次（以及之后每一次，除非重新设置）系统流程输入都会套用这些选项。

字符串型输入的对应版本为 `FLOWINPUTS`（本仓库实现：`FLOWINPUTS(<是否字符串模式>, "<默认字符串>")`）。

EE readme 中将其列为命令；本仓库源码把它实现为式中函数（调用后返回 0），通过设置 `Process` 对象上的标志位生效，且设置后不会自动复位。

## 用法

### `FLOWINPUT(<默认值>{, <左键视为 Enter>{, <右键可跳过>{, <强制跳过>}}})`
```erb
;为系统流程输入设置默认值 0，允许左键当 Enter、右键跳过
FLOWINPUT(0, 1, 1)
```

### `FLOWINPUTS(<是否字符串模式>, "<默认字符串>")`
```erb
;下一次系统流程输入按字符串输入处理，默认值为 "abc"
FLOWINPUTS(1, "abc")
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:335`（`["FLOWINPUT"] = new FlowInputMethod()`）；`FLOWINPUTS` 在 336 行
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7423`（`FlowInputMethod`，`#region EE_SystemInput拡張`）；`FLOWINPUTS` 在 7446 行；消费点 `Runtime/Script/Process.SystemProc.cs:108-149`（`setWaitInput()`）

```text
# FlowInputMethod（FLOWINPUT）
构造: ReturnType = 整数; argumentTypeArrayEx = [ [int, int, int, int], OmitStart = 1 ]
      CanRestructure = false

GetIntValue(exm, arguments):
    Process.flowinputDef       = arguments[0].GetIntValue(exm)
    if arguments.Count > 1: Process.flowinput          = (arguments[1] != 0)
    if arguments.Count > 2: Process.flowinputCanSkip   = (arguments[2] != 0)
    if arguments.Count > 3: Process.flowinputForceSkip = (arguments[3] != 0)
    return 0

# FlowInputsMethod（FLOWINPUTS）
构造: argumentTypeArrayEx = [ [int, string], OmitStart = 1 ]
GetIntValue(exm, arguments):
    Process.flowinputString    = (arguments[0] != 0)
    if arguments.Count > 1: Process.flowinputDefString = arguments[1].GetStrValue(exm)
    return 0

# 消费点：Process.setWaitInput()（系统流程等待输入时调用）
setWaitInput():
    req = new InputRequest
    if Process.flowinput:                       # 曾用 FLOWINPUT 设置过
        req.HasDefValue = true
        req.DefIntValue = Process.flowinputDef
        req.MouseInput  = true                  # 左键视为 Enter
        req.DefStrValue = Process.flowinputDefString
    req.InputType     = flowinputString ? StrValue : IntValue
    req.IsSystemInput = true
    if Process.flowinputForceSkip:              # 强制跳过：不等输入直接用默认值
        systemResult = req.DefIntValue
        if flowinputString: RESULTS = req.DefStrValue
    else if Process.flowinputCanSkip 且 Console.MesSkip:   # 正在右键跳过中
        systemResult = req.DefIntValue
        if flowinputString: RESULTS = req.DefStrValue
    console.WaitInput(req)
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准。
- 文档与源码形态差异：readme 把 `FLOWINPUT` 描述为命令（无括号、无返回值），本仓库实现为式中函数（有括号、恒返回 0）。行为实质相同（设置系统输入选项），但书写形式不同，EE 发行版与本仓库需按各自形态书写。
- readme 只记载 3 个参数；本仓库源码额外支持第 4 参数「强制跳过」（`flowinputForceSkip`），生效时连等待都省去，直接把默认值代入结果。
- 源码中 `flowinput*` 标志一旦设置不会被复位（`Runtime/Script/Process.SystemProc.cs:108-113` 初始化后无清零逻辑），设置将持续影响其后所有系统流程输入，直到再次调用 `FLOWINPUT`/`FLOWINPUTS` 覆盖。
