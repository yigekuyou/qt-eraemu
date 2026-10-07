# BREAKBUTTON

- **类别**：EE 扩展命令（Emuera 枚举成员；EE 扩展；两套中文文档未收录，语义据源码推定）
- **签名**（推定自 `FunctionArgType.EXPRESSION_NULLABLE`，`Runtime/Script/Statements/ArgumentBuilder.cs:1478`）：`BREAKBUTTON {<任意表达式>}`
- **文档来源**：无任何文档收录。`eraTW/README集/EmueraEE Readme/`（readme 与 changelog）及两套中文文档均未出现本命令名；语义据源码推定（指令类行为 + 控制台「按钮世代」机制）。

## 语义

立刻使**当前画面上已绘制的所有按钮全部作废**（不可再点击、不可再被按钮输入接受）。

- 实现上只做一件事：调用 `forceUpdateGeneration()`，把「按钮世代计数」+1 并把「当前有效世代」设为新值（`UI/Game/EmueraConsole.cs:535`）。按钮在绘制时记录当时的世代号（`UI/Game/ConsoleButtonString.cs:48/62/78/92`），此后只有世代号等于当前有效世代的按钮才可被选中/点击（例如点击判定 `UI/Game/EmueraConsole.cs:973`、`:1031`、`:1595`）。
- 因此 `BREAKBUTTON` 之后必须**重新绘制**按钮（`PRINTBUTTON` 等）才会重新出现可点击的按钮；之前画的按钮视觉上仍在画面上，但已失效。
- 与 `BINPUT`/`BINPUTS`/`ONEBINPUT`/`ONEBINPUTS` 的配合：这些命令扫描画面按钮时只认最新一代，且从后往前扫遇到旧世代按钮即停止（`Runtime/Script/Statements/Instraction.Child.cs:2273` 等）。`BREAKBUTTON` 后若不重画按钮，这些命令会认为「画面上一个按钮都没有」，从而走默认值或抛 CodeEE。
- 与 `INPUT`/`INPUTS` 的「世代自动推进」（`UI/Game/EmueraConsole.cs:538-576` 的 `newGeneration`，按输入类型自动作废旧选择项）互补：那是输入时的自动行为，本命令是**随时手动、立即**作废。
- 参数用 `EXPRESSION_NULLABLE`（0 或 1 个任意表达式）：**参数被解析但完全不被使用**（源码 `DoInstruction` 不读 `func.Argument`）；写出表达式也不会有求值副作用。给 2 个以上参数只告警（非致命），同样不影响行为。
- flag = `METHOD_SAFE | EXTENDED`：按源码注释（`Runtime/Script/Statements/FunctionIdentifier.cs:21`）「`#Function` 中可调用的命令」，即本命令可在 `#FUNCTION` 里调用（但建议只用于 UI 流程）。

## 用法

### `BREAKBUTTON`
```erb
PRINTL 请选择。
PRINTBUTTON "[0] はい", 0
PRINTBUTTON "[1] いいえ", 1
BREAKBUTTON          ;此前的 [0]/[1] 立即失效
PRINTBUTTON "[2] やめる", 2
BINPUT               ;只能点到 [2]（旧按钮已作废）
```
### `BREAKBUTTON {<任意表达式>}`（参数被忽略）
```erb
;写法合法但参数无作用
BREAKBUTTON 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:385`（枚举 `BREAKBUTTON`，`#region EE`）；`Runtime/Script/Statements/FunctionIdentifier.cs:431`（`addFunction(FunctionCode.BREAKBUTTON, new BREAKBUTTON_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3018`（`BREAKBUTTON_Instruction`，`#region BREAKBUTTON` 自 `:3017`）；参数构建 `FunctionArgType.EXPRESSION_NULLABLE`（`Runtime/Script/Statements/ArgumentBuilder.cs:1478`，`argb` 登记于 `:210`）
- 控制台：`UI/Game/EmueraConsole.cs:535`（`forceUpdateGeneration`）；世代字段与注释 `:526-533`；绘制时记录世代 `UI/Game/ConsoleButtonString.cs:48-49` 等；点击判定 `:973`/`:1031`

```text
构造 BREAKBUTTON_Instruction:
    ArgBuilder = EXPRESSION_NULLABLE（0 或 1 个任意表达式；多余参数只告警）
    flag = METHOD_SAFE | EXTENDED

DoInstruction(exm, func, state):
    exm.Console.forceUpdateGeneration()      # 参数完全未使用

EmueraConsole.forceUpdateGeneration()（EmueraConsole.cs:535）:
    newButtonGeneration++
    lastButtonGeneration = newButtonGeneration
    updatedGeneration = true

世代语义（EmueraConsole.cs:526-527 注释、ConsoleButtonString.cs:48-49）:
    绘制按钮时: button.Generation = 当时的 NewButtonGeneration; 并调用 UpdateGeneration()（把有效世代推进到该值）
    点击/选择判定: 只有 button.Generation == LastButtonGeneration 的按钮可被点击
    → forceUpdateGeneration 使「有效世代」跳到更大的新值，所有已绘制按钮（旧世代）立即不可点击/不可被按钮输入采用
```

## 备注

- 两套中文文档均未收录本命令；EE readme/changelog 也未提到它，**语义据源码推定**（推定依据：`forceUpdateGeneration` 的调用点、世代字段的注释「これと世代が一致しない選択肢は選択できない」（`UI/Game/EmueraConsole.cs:527`）以及各按钮输入命令的世代过滤逻辑）。
- 源码中 `forceUpdateGeneration` 的既有用途是报错时清空可点击状态（`UI/Game/EmueraConsole.cs:513`）与绘图/标题返回等场景（`:1376`）；本命令把它暴露给脚本，属 EE 的脚本化入口。
- 与 `CLEARLINE` 类命令的区别：本命令**不删除任何显示内容**，只作废按钮的「可操作性」；画面刷新由后续的绘制/输入命令触发（本命令不主动刷新画面——推定：源码无刷新调用）。
- 与 `BINPUT.md:91` 提到的世代机制一致：`BINPUT` 系命令的按钮扫描依赖 `LastButtonGeneration`，因此 `BREAKBUTTON` 会让它们看不到旧按钮。
- 名字可理解为「break（断开）button」，即断开旧按钮与当前输入流程的关联。
