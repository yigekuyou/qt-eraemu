# FORCE_BEGIN

- **类别**：EE 扩展命令
- **签名**：
  - `FORCE_BEGIN <系统函数名>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・FORCE_BEGIN システム関数」；`EmueraEE_changelog.txt`（EM+EE v12 追加）。ecd 套件与 zh 套件均未收录本命令。

## 语义

不受 `BEGIN` 的状态制约、强制执行 `BEGIN`。普通 `BEGIN` 只能在系统状态允许（`__CAN_BEGIN__`）时使用，例如不能在 TRAIN 中途直接 `BEGIN SHOP`；`FORCE_BEGIN` 跳过这一检查，把流程切换到指定的系统函数（`SHOP`/`TRAIN`/`AFTERTRAIN`/`ABLUP`/`TURNEND`/`FIRST`/`TITLE`）。

由于是刻意破坏正常流程（不经过存档、事件收尾等常规约束），可能引发预期之外的问题，文档也明确警告了这一点。`BEGIN TITLE` 本来就随处可用，无需 FORCE。

## 用法

### `FORCE_BEGIN <系统函数名>`
关键字写法与 `BEGIN` 相同（`SHOP`、`TRAIN`、`AFTERTRAIN`、`ABLUP`、`TURNEND`、`FIRST`、`TITLE`）。
```erb
;无视当前状态强制回到商店流程
FORCE_BEGIN SHOP
```
非法关键字会报错（与 `BEGIN` 相同）。

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:373`（枚举 `FORCE_BEGIN`）；`Runtime/Script/Statements/FunctionIdentifier.cs:420`（`addFunction(FunctionCode.FORCE_BEGIN, new FORCE_BEGIN_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3056`（`FORCE_BEGIN_Instruction`，`#region EE`）；强制的实质在 `Runtime/Script/Process.State.cs:181`（`SetBegin(string, bool force)`）与 216-223 行（`SetBegin(BeginType, bool force)`）

```text
构造: ArgBuilder = STR 参数（常量字符串关键字）; flag = FLOW_CONTROL

DoInstruction(exm, func, state):
    keyword = func.Argument.ConstStr          # 已 Trim/ToUpper
    state.SetBegin(keyword, true)             # force = true（普通 BEGIN 传 false）
    state.Return(0)                           # 结束当前函数调用链
    Console.ResetStyle()

SetBegin(keyword, force):
    switch keyword:
        "SHOP":       卸载临时加载的常驻/图像资源名; SetBegin(SHOP, force)
        "TRAIN":      SetBegin(TRAIN, force)
        "AFTERTRAIN": SetBegin(AFTERTRAIN, force)
        "ABLUP":      SetBegin(ABLUP, force)
        "TURNEND":    SetBegin(TURNEND, force)
        "FIRST":      卸载临时加载的常驻/图像资源名; SetBegin(FIRST, force)
        "TITLE":      SetBegin(TITLE, force)
        default: throw CodeEE(「BEGIN 的关键字不合法」)

SetBegin(type, force):
    switch type:
        SHOP/TRAIN/AFTERTRAIN/ABLUP/TURNEND/FIRST:
            if force == true: break           # ← FORCE_BEGIN 与 BEGIN 的唯一差别
            if 系统状态无 __CAN_BEGIN__:
                errmes = "BEGIN"; goto err    # 抛 CodeEE（当前函数不可用 BEGIN）
        TITLE: break                          # 1.729 起 TITLE 处处可用
    begintype = type
    return
    err: throw CodeEE(「在函数 <当前函数> 中不能使用 BEGIN」)
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准，两者一致（readme 的「BEGINの制約を受けずに強制的にBEGINを実行する」对应 `Runtime/Script/Process.State.cs` 中 `if (force == true) break;` 跳过 `__CAN_BEGIN__` 检查）。
- 与普通 `BEGIN_Instruction`（`Runtime/Script/Statements/Instraction.Child.cs:3037`）逐行相同，唯一差异是 `SetBegin(keyword, true)`；本仓库普通 `BEGIN` 也已改为调用带 force 参数的重载（传 `false`），行为与原版一致。
- CHANGELOG 将其归类为「関数追加」，实际是指令（instruction）。
