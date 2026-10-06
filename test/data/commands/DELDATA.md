# DELDATA

- **类别**：命令（EE 扩展命令）
- **签名**：
  - `DELDATA <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「游戏存档的操作」小节（「删除第 1 参数所示编号的文件中的数据。文件不存在也不会出错」）；zh 套件未收录本命令。

## 语义

删除第 1 参数所示编号对应的存档文件（即 `SAVEGAME`/`SAVEDATA` 保存到该编号的数据）。指定的文件不存在时不出错、静默返回。

编号必须是不小于 0 且不超过 `int.MaxValue` 的值，否则抛出 CodeEE 错误。如果目标文件是只读文件，删除失败并抛出 CodeEE 错误。与 `SAVEGAME`/`LOADGAME` 一样不提供覆盖确认，删除前如需确认请在脚本侧用 `CHKDATA` 自行实现。

## 用法

### `DELDATA <数值表达式>`
- `<数值表达式>`：存档编号。对应文件不存在时不报错。
```erb
;删除 14 号存档（若存在）
DELDATA 14
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:230`（`new DELDATA_Instruction()`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:140`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1996`（`DELDATA_Instruction`）；核心删除逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:2463`（静态方法 `DelData(int)`）

```text
指令类 DELDATA_Instruction:
    参数构造器 = INT_EXPRESSION
    flag = METHOD_SAFE | EXTENDED

DoInstruction:
    若 func.Argument.IsConst: target = 常量整数
    否则:                    target = 表达式项求值
    target32 = toUInt32inArg(target, "DELDATA", 1)
        # target < 0          → 抛出 CodeEE（"第1参数为负数"）
        # target > int.MaxValue → 抛出 CodeEE（"第1参数过大"）
    VariableEvaluator.DelData(target32)

DelData(dataIndex):                        # 静态方法
    filepath = getSaveDataPath(dataIndex)  # 按编号拼出存档文件路径
    若 !File.Exists(filepath):
        return                             # 文件不存在 → 静默返回（与文档一致）
    att = File.GetAttributes(filepath)
    若 att 含 ReadOnly 标志:
        抛出 CodeEE（"文件为只读，无法删除"）
    File.Delete(filepath)
```

## 备注

- 文档说「文件不存在也不会出错」，与源码的 `File.Exists` 检查一致；但文档没有提到**只读文件会报错**，这是从源码补充的行为。
- 编号检查由通用助手 `toUInt32inArg`（`Runtime/Script/Statements/Instraction.Child.cs:1893`）完成，负数与超int上限都会报错。
- zh 套件未收录本命令。
