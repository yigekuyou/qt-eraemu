# SWAPCHARA

- **类别**：命令
- **签名**：SWAPCHARA `<数值表达式>`, `<数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`（`### SWAPCHARA <数值表达式>, <数值表达式>` 小节）；Era-Chinese-Documentation 未收录本命令

## 语义

交换指定的两个角色在角色列表中的登录编号（位置）。参数是角色在列表中的位置编号（`X` 号位与 `Y` 号位），交换后这两个位置上的角色数据（含全部角色变量）整体对调。

两个位置编号超出当前角色数范围（`< 0` 或 `>= 角色数`）时抛错。两编号相同则不做任何事。

## 用法

### SWAPCHARA `<数值表达式>`, `<数值表达式>`

- 第 1、2 参数：要交换的两个角色位置编号。
- 副作用：角色列表中两位置的角色整体对调。

```erb
;假设只有 MASTER
ADDCHARA 10
ADDCHARA 11
PRINTFORML NO:1 = {NO:1}, NO:2 = {NO:2}   ;NO:1 = 10, NO:2 = 11
SWAPCHARA 1,2
PRINTFORML NO:1 = {NO:1}, NO:2 = {NO:2}   ;NO:1 = 11, NO:2 = 10
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:266`（`new SWAPCHARA_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1460`（类 `SWAPCHARA_Instruction`，参数走 `SP_SWAP` 解析器）；实际交换在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1196`（`VariableEvaluator.SwapChara`）

```text
SWAPCHARA_Instruction.DoInstruction:
    arg = (SpSwapCharaArgument)func.Argument
    x = arg.X.GetIntValue(exm)          // 第 1 个角色位置
    y = arg.Y.GetIntValue(exm)          // 第 2 个角色位置
    exm.VEvaluator.SwapChara(x, y)

VariableEvaluator.SwapChara(x, y):
    若 x < 0 || x >= 角色数 || y < 0 || y >= 角色数:
        throw new CodeEE(SWAPCHARA 的参数超出角色编号范围)
    若 x == y: return
    交换 varData.CharacterList[x] 与 varData.CharacterList[y]
      （列表元素为 CharacterData 对象，交换即整体对调两角色的全部数据）
```

## 备注

- 文档示例与源码语义一致：交换的是「登录编号」（列表位置），`TARGET`/`ASSI` 等保存位置的变量在此处**不会**自动追随（与 SORTCHARA 不同），移动角色后需自行修正。
- Era-Chinese-Documentation 套件未收录本命令。
