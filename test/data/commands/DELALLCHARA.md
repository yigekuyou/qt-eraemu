# DELALLCHARA

- **类别**：命令（EE 扩展命令）
- **签名**：
  - `DELALLCHARA`
- **文档来源**：`ecd/docs/translation/Command.md`「角色操作·引用」小节（「删除所有已登录的角色」）；zh 套件未收录本命令。

## 语义

删除当前已登录的全部角色（清空角色列表，`CHARANUM` 变为 0）。无参数。

ecd 文档指出它等同于下面的脚本：

```erb
REPEAT CHARANUM
  DELCHARA 0
REND
```

但源码实现是一次性 `Dispose()` 所有 `CharacterData` 后清空列表，不经过逐个下标移动，比循环 `DELCHARA` 更直接。

## 用法

### `DELALLCHARA`
- 无参数。
```erb
;清空全部角色后重新注册
DELALLCHARA
ADDCHARA 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:314`（`argb[FunctionArgType.VOID]`，flag = `METHOD_SAFE | EXTENDED`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:236`）
- 实现：switch-case 分发于 `Runtime/Script/Process.ScriptProc.cs:258`；核心逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1103`（`DelAllCharacter()`）

```text
ScriptProc 执行期:
    case FunctionCode.DELALLCHARA:
        vEvaluator.DelAllCharacter()

DelAllCharacter():
    若 varData.CharacterList.Count == 0:
        return                      # 空列表时什么也不做
    对列表中每个 CharacterData:
        chara.Dispose()             # 释放角色数据
    varData.CharacterList.Clear()   # 清空角色列表（CHARANUM → 0）
```

## 备注

- 本命令走 `Runtime/Script/Process.ScriptProc.cs` 的 switch 分发（无专用指令类），与 `DELCHARA`（复用 `ADDCHARA_Instruction`）的实现路径不同。
- 源码不自动处理 `TARGET`、`ASSI`、`MASTER` 等引用变量，删除后这些指针可能指向已清空的列表（与 `PICKUPCHARA` 的自动追随不同），脚本侧需要自行置回 `-1`。
- zh 套件（Era-Chinese-Documentation）未收录本命令。
