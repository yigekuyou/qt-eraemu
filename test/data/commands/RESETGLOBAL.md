# RESETGLOBAL

- **类别**：命令
- **签名**：RESETGLOBAL
- **文档来源**：`ecd/docs/translation/Command.md`（RESETGLOBAL 小节）；Era-Chinese-Documentation 未单独收录该命令

## 语义

初始化全局变量：把 `GLOBAL` 的所有元素赋值为 0，`GLOBALS` 的所有元素赋值为空字符串。不影响其他任何变量。

## 用法

### RESETGLOBAL
无参数。

```erb
PRINTL 清空全局设置。
RESETGLOBAL
PRINTL GLOBAL 与 GLOBALS 已初始化。
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:234` → `new RESETGLOBAL_Instruction()`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:203`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1879`（`RESETGLOBAL_Instruction`）；核心逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1168`（`VariableEvaluator.ResetGlobalData()`）

```text
类 RESETGLOBAL_Instruction:
  构造: 参数构造器 = VOID; 标志 = METHOD_SAFE | EXTENDED

  DoInstruction(exm, func, state):
    exm.VEvaluator.ResetGlobalData()   // 见下

VariableEvaluator.ResetGlobalData():
    varData.RemoveEMGlobalData()   // （EM 私家版扩展）清除存档指定的全局关联数组/XML/DataTable
    varData.RemoveEMStaticData()   // （EM 私家版扩展）清除静态关联数组/XML/DataTable
    varData.SetDefaultGlobalValue()   // GLOBAL 全部置 0，GLOBALS 全部置空串
```

## 备注

- 本仓库在标准行为之外额外清除了 EM 私家版扩展的全局/静态关联数组等数据，这是文档未提及的实现差异。
- Era-Chinese-Documentation 未收录本命令。
