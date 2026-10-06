# RESET_STAIN

- **类别**：命令
- **签名**：RESET_STAIN `<数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`（RESET_STAIN 小节）；Era-Chinese-Documentation 未收录该命令

## 语义

将参数所指定角色的 `STAIN` 变量进行初始化。初始化行为与 `BEGIN TRAIN` 类似，根据 `_replace.csv` 中 `汚れの初期値`（污垢初始值）的设置来赋值。若指定的角色编号不存在（未登录），抛出运行时错误。

## 用法

### RESET_STAIN `<数值表达式>`
- `<数值表达式>`：已登录角色编号（`TARGET` 等角色索引）。

```erb
;把 MASTER（角色 0）的 STAIN 恢复为 _replace.csv 设定的初始值
RESET_STAIN 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:320` → `argb[FunctionArgType.INT_EXPRESSION], METHOD_SAFE | EXTENDED`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:242`
- 实现：`Runtime/Script/Process.ScriptProc.cs:528`（switch-case `FunctionCode.RESET_STAIN`）；核心逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1678`（`VariableEvaluator.SetDefaultStain`）与 `:1661`（私有 `setDefaultStain`）

```text
switch (func.Code) case RESET_STAIN:
    iValue = func.Argument.IsConst ? func.Argument.ConstInt
                                   : ((ExpressionArgument)func.Argument).Term.GetIntValue(exm)
    vEvaluator.SetDefaultStain(iValue)

VariableEvaluator.SetDefaultStain(no):
    if no < 0 or no >= varData.CharacterList.Count:
        throw CodeEE("引用了未定义的角色")     // trerror.RefUndefinedChara
    chara = varData.CharacterList[no]
    setDefaultStain(chara)

setDefaultStain(chara):    // 静态私有方法
    array = chara 的 STAIN 整数数组
    if array.Length >= Config.StainDefault.Count:      // StainDefault 即 _replace.csv 的汚れの初期値
        Config.StainDefault.CopyTo(array)              // 前若干个元素按 CSV 初始值赋值
        for i = StainDefault.Count .. array.Length-1:
            array[i] = 0                               // 多余元素补 0
    else:
        for i = 0 .. array.Length-1:
            array[i] = Config.StainDefault[i]          // 数组比 CSV 设定短时只覆盖前段
```

## 备注

- 文档说「根据 `_replace.csv` 中汚れの初期値的设置来赋值」，与源码一致（`Config.StainDefault` 即读取自该配置项）。
- 文档只提到错误发生在角色不存在时；源码还处理了 STAIN 数组长度与 CSV 设定数量不一致的两种情况（多补 0 / 截断），文档未提及。
- Era-Chinese-Documentation 未收录本命令。
