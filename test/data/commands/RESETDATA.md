# RESETDATA

- **类别**：命令
- **签名**：RESETDATA
- **文档来源**：`ecd/docs/translation/Command.md`（RESETDATA 小节）；Era-Chinese-Documentation 未单独收录该命令

## 语义

初始化除 `GLOBAL` / `GLOBALS`（以及本仓库扩展的静态关联数组等）以外的所有变量。具体为：删除所有已登录角色，将局部变量与通常变量全部以数值 0 或空字符串填充；像 `PALAMLV`、`STR` 等在 CSV/头文件中设定了初始值的变量恢复为其初始值。同时会重置控制台的文字样式（`ResetStyle`）。

## 用法

### RESETDATA
无参数。

```erb
PRINTL 重新开始新游戏。
RESETDATA
PRINTL 所有变量已初始化。
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:233` → `new RESETDATA_Instruction()`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:202`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1864`（`RESETDATA_Instruction`）；核心逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1149`（`VariableEvaluator.ResetData()`）

```text
类 RESETDATA_Instruction:
  构造: 参数构造器 = VOID; 标志 = METHOD_SAFE | EXTENDED

  DoInstruction(exm, func, state):
    exm.VEvaluator.ResetData()    // 见下
    exm.Console.ResetStyle()      // 重置控制台文字样式

VariableEvaluator.ResetData():
    // 注释：不初始化 GLOBAL 才是符合预期的做法
    varData.RemoveEMSaveData()                  // （EM 私家版扩展）清除存档指定的关联数组/XML/DataTable
    varData.SetDefaultLocalValue()              // 局部变量（@函数内的 LOCAL 等）恢复默认值
    varData.SetDefaultValue(constant)           // 通常变量恢复默认值（CSV 初始值者优先）
    foreach chara in varData.CharacterList:
        chara.Dispose()                          // 释放每个角色
    varData.CharacterList.Clear()               // 清空角色列表
```

## 备注

- 与源码一致，`RESETDATA` 不会初始化 `GLOBAL`/`GLOBALS`（那是 `RESETGLOBAL` 的职责）；源码中对此有明确注释。
- 本仓库（EM 系分支）在标准行为之外额外清除了 EM 私家版扩展保存的关联数组/XML/DataTable 数据（`RemoveEMSaveData`），这是文档未提及的实现差异。
- Era-Chinese-Documentation 未收录本命令（仅在 Flow.md 中提到 `RESETDATA` 语义作对照）。
