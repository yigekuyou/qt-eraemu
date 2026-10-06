# CALLEVENT

- **类别**：命令
- **签名**：
  - `CALLEVENT <事件函数名>`
- **文档来源**：两套文档均无本命令专节。`ecd` 套件仅在 `Error_Index.md` 提到相关错误（「EVENT関数中にCALLEVENT命令は使用できません」警告、「EVENT関数の解決前にCALLEVENT命令が行われました」错误）；`Era-Chinese-Documentation` 套件未收录。`ecd ERB_Internal_Process.md`「事件函数」表提供了事件函数的背景语义。

## 语义

按名字调用**事件函数**（可多重定义的事件函数，如 `@EVENTFIRST`、`@EVENTCOM` 之外自定义的带 `EVENT` 前缀函数）：与 `CALL` 只能调用普通函数相对，`CALLEVENT` 专门用于事件函数，会把该名字下定义的全部事件函数按定义顺序依次执行一遍，不接收参数、不返回值。

限制与错误：
- 在事件函数内部使用 `CALLEVENT` 是不允许的（解析期给出警告，运行期由 `IntoFunction` 拦截）。
- 目标名字不是事件函数而是普通函数时抛 CodeEE（对非事件函数使用 CALLEVENT）。
- 目标名字完全不存在时静默通过（什么都不做，不报错）。
- 在事件函数标签尚未解析完毕的时机（如 ERB 加载中）执行会报错。

## 用法

### `CALLEVENT <事件函数名>`
- `<事件函数名>`：字符串常量，不带 `@`。对应以事件函数形式定义（可多重定义）的函数。
```erb
@SHOW_SHOP
  CALLEVENT SHOW_MAIN
  ;依次执行所有定义为 @SHOW_MAIN 的事件函数

@SHOW_MAIN
  PRINTL 事件函数定义 1

@SHOW_MAIN
  PRINTL 事件函数定义 2
;两个定义都会被执行
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:351`（`new CALLEVENT_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3718`（`CALLEVENT_Instruction`）；事件函数解析在 `Runtime/Script/Process.CalledFunction.cs:83`（`CalledFunction.CallEventFunction`）

```text
指令类 CALLEVENT_Instruction:
    flag = FLOW_CONTROL | EXTENDED      # 注意无 METHOD_SAFE
    参数为 STR（常量字符串）

    SetJumpTo(...):                     # 解析期检查
        若当前所在函数标签 label.IsEvent（本身就是事件函数）:
            ParserMediator.Warn("事件函数中不能使用 CALLEVENT 命令", 级别2)
        # 源码注释：事件函数中 CALL 过去的目的地再次 CALLEVENT 的情形由 IntoFunction 拦截

    DoInstruction(exm, func, state):
        labelName = func.Argument.ConstStr
        call = CalledFunction.CallEventFunction(Process, labelName, func)
        若 call == null:
            return                      # 名字不存在 → 静默通过
        state.IntoFunction(call, null, null)   # 不带参数；事件列表逐个执行

CallEventFunction(parent, label, retAddress):
    called = 新建 CalledFunction(label)
    called.eventLabelList = LabelDictionary.GetEventLabels(label)
    若 eventLabelList == null:
        若 GetNonEventLabel(label) != null:
            抛 CodeEE（"对非事件函数 @label 使用了 CALLEVENT"，附文件名与行号）
        返回 null                       # 普通函数/不存在 → 上层静默处理
    called.counter = -1; called.group = 0
    called.ShiftNext()                  # 定位到第一个定义
    called.TopLabel = called.CurrentLabel
    called.IsEvent = true               # IntoFunction 中按事件函数逐个执行全部定义
```

## 备注

- 本命令在两套文档中都没有语义小节，以上语义完全基于源码；`ecd ERB_Internal_Process.md` 只说明「`@CALLTRAINEND` 不是事件函数，不能多重定义；其余带 EVENT 前缀的多为事件函数，可以定义多个，全部都会被调用」，与 `CALLEVENT` 的批量执行行为吻合。
- flag 为 `FLOW_CONTROL | EXTENDED`，不含 `METHOD_SAFE`；这一点与同样改变流程的 `CALL`（`FLOW_CONTROL | FORCE_SETARG`）一致，只有 `CALLF` 这类不改变流程的指令才带 `METHOD_SAFE`。
- 与 `CALL` 的关键差异：`CALL` 对事件函数报错（`CallFunction` 中 `GetEventLabels != null` 抛 CodeEE，可用兼容选项 `CompatiCallEvent` 关闭该报错），`CALLEVENT` 则对普通函数报错、对不存在的名字静默通过——默认行为两者互斥。
