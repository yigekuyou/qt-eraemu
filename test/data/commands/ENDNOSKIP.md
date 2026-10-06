# ENDNOSKIP

- **类别**：命令
- **签名**：`ENDNOSKIP`
- **文档来源**：`ecd/docs/translation/Command.md`（「显示处理·字体处理·显示方式参考」节 `### NOSKIP`、`### ENDNOSKIP`，另见 `### SKIPDISP` 中的配合说明）；`Era-Chinese-Documentation` 未收录。

## 语义

结束由 `NOSKIP` 开始的「忽略显示忽略开关」区间。被 `NOSKIP`～`ENDNOSKIP` 包围的区间即使在 `SKIPDISP 1`（忽略输出）状态下也会正常显示，主要用于必须让玩家看到并响应 `INPUT` 的场合。`ENDNOSKIP` 执行时，若进入 `NOSKIP` 区间前本来就处于忽略状态（`NOSKIP` 保存的原状态为真），则恢复忽略状态；`NOSKIP` 区间本身不改变 `SKIPDISP` 的设定。`ENDNOSKIP` 必须与配对的 `NOSKIP` 一起使用，缺失配对时运行期抛错。不能嵌套 `NOSKIP`。

## 用法

### `ENDNOSKIP`

无参数。

```erb
SKIPDISP 1
PRINTL 这段被忽略不显示
NOSKIP
PRINTL 即使 SKIPDISP 1 也会显示
INPUT            ;没有 NOSKIP 的话玩家会卡在看不见的输入上
ENDNOSKIP
PRINTL 又回到忽略状态
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:327` → `argb[FunctionArgType.VOID], METHOD_SAFE | EXTENDED | PARTIAL`；switch 分发在 `Runtime/Script/Process.ScriptProc.cs:590`
- 实现：`Runtime/Script/Process.ScriptProc.cs:590`（`case FunctionCode.ENDNOSKIP`，运行于 `PrintProc` 的 skip 状态处理中）；装载期检查嵌套在 `Runtime/Script/Loader/ErbLoader.cs:1436`（`case FunctionCode.NOSKIP`）

```text
装载期（ErbLoader）:
    遇到 NOSKIP:
        若 nestStack 中已有 NOSKIP → 警告（NestedNoskip，不允许嵌套）
        压栈（供 ENDNOSKIP 找不到配对时报警）
    （配对关系由 JumpTo 建立：NOSKIP 的 JumpTo 指向配对 ENDNOSKIP）

运行期（ScriptProc 的 skipPrint 处理）:
    进入 NOSKIP 时:
        若 func.JumpTo == null（没有配对的 ENDNOSKIP）
            抛 CodeEE（MissingEndnoskip）
        saveSkip = skipPrint        ;保存进入前的忽略状态
        若 skipPrint 为真 → skipPrint = false   ;临时恢复显示
    遇到 ENDNOSKIP 时:
        若 func.JumpTo == null（没有配对的 NOSKIP）
            抛 CodeEE（MissingNoskip："ENDNOSKIP" 缺少对应的 NOSKIP）
        若 saveSkip 为真 → skipPrint = true
            ;仅当进入 NOSKIP 前就处于忽略状态时才恢复忽略
```

## 备注

- `ENDNOSKIP` 注册为 `PARTIAL` 但实际运行期有动作（在 switch 分发中处理），与 `ENDLIST`/`ENDDATA` 的纯装载期结构不同。
- ecd 文档说 NOSKIP 区间「不会影响 SKIPDISP 的状态」：实现上确实不动 `SKIPDISP` 设定本身，只在区间内临时把 `skipPrint` 置假、离开时按 `saveSkip` 恢复，语义一致。
- 若 `SKIPDISP 1` 期间执行 `INPUT` 会直接警告出错（见 `### SKIPDISP`），因此需要输入的区间应使用 `NOSKIP`～`ENDNOSKIP` 包围（文档推荐前者而非临时 `SKIPDISP 0`）。
- `ENDNOSKIP` 缺配对 NOSKIP 是运行期 CodeEE 错误（JumpTo 为 null 时抛出），不只是装载警告。
