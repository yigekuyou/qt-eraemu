# COPYCHARA

- **类别**：命令
- **签名**：`COPYCHARA <数值表达式>, <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「COPYCHARA」小节；`Era-Chinese-Documentation` 未收录本命令

## 语义

把第 1 参数指定登录编号（registration index，0 ～ CHARANUM-1）的角色的全部数据，复制覆盖到第 2 参数指定登录编号的角色上。目标角色原有的所有变量数据全部丢失，被源角色数据替换。两个编号都必须是已存在的登录编号，否则报错。若想把复制目标设为"新增加的角色"，应使用 `ADDCOPYCHARA`。

## 用法

### `COPYCHARA <复制源登录编号>, <复制目标登录编号>`
- 第 1 参数：源角色的登录编号。
- 第 2 参数：目标角色的登录编号（会被覆盖）。
```erb
;把 0 号角色的数据完整复制到 3 号角色
COPYCHARA 0, 3
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:267` → `new COPYCHARA_Instruction()`（ArgBuilder = SP_SWAP，flag = METHOD_SAFE | EXTENDED）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1476`（`COPYCHARA_Instruction`）；核心逻辑转调 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1179`（`VariableEvaluator.CopyChara`）

```text
DoInstruction(exm, func, state):
    arg = (SpSwapCharaArgument)func.Argument
    x = arg.X.GetIntValue(exm)     // 源登录编号
    y = arg.Y.GetIntValue(exm)     // 目标登录编号
    exm.VEvaluator.CopyChara(x, y)

VariableEvaluator.CopyChara(x, y):
    若 x < 0 或 x >= 角色列表.Count: 抛出 CodeEE("COPYCHARA指定的源角色不存在")
    若 y < 0 或 y >= 角色列表.Count: 抛出 CodeEE("COPYCHARA指定的目标角色不存在")
    角色列表[x].CopyTo(角色列表[y], varData)   // 逐变量深拷贝，覆盖目标角色的全部数据
```

## 备注

- 与 `ADDCOPYCHARA`（先 `AddPseudoCharacter()` 追加新角色再拷贝，见 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1188`）不同，`COPYCHARA` 不增减角色数，只做覆盖。
- ecd 文档与源码一致；zh 文档套件未收录该命令。
