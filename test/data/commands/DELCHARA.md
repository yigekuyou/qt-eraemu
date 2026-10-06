# DELCHARA

- **类别**：命令
- **签名**：
  - `DELCHARA <数值表达式>(, <数值表达式>, <数值表达式>, ...)`
- **文档来源**：`ecd/docs/translation/Command.md`「角色操作·引用」小节；zh 套件 `ERB_File_Format.md`「角色管理」小节（「`DELCHARA`：删除一个由`ADDCHARA`或其他方法添加的角色」，带示例）。

## 语义

删除角色。参数是角色的**登录编号**（角色列表中的位置，从 0 开始），而不是 CSV 编号。Eramaker 时代一次只能删一个，Emuera 允许一次给出多个参数、同时删除多个角色。

指定的登录编号越界（小于 0 或不小于当前 `CHARANUM`）时抛出 CodeEE 错误；多参数形式下还检查重复删除（同一个角色在参数表中出现两次会报错）。删除后其后角色的登录编号整体前移，`CHARANUM` 减少；`TARGET`、`ASSI`、`MASTER` 等指针变量不会自动追随，需要脚本自行维护。

## 用法

### `DELCHARA <数值表达式>(, <数值表达式>, ...)`
- `<数值表达式>`：要删除的角色的登录编号。可写多个，用逗号分隔，一次删除多个角色。
```erb
;编号为0的角色为主人公，3为小红、5为小刚、6为小垃圾
DELCHARA 2
PRINTFORML 当前共有{CHARANUM}名角色。
;Emuera 扩展：一次删除多个
DELCHARA 0, 1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:215`（`new ADDCHARA_Instruction(false, true)`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:75`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1400`（复用 `ADDCHARA_Instruction`，构造参数 `flagSp=false, flagDel=true`）；删除逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1078`（`DelCharacter(long)`）与 `:1086`（`DelCharacter(long[])`）

```text
指令类 ADDCHARA_Instruction(flagSp=false, flagDel=true):
    参数构造器 = INT_ANY（任意个整数表达式）
    flag = METHOD_SAFE

DoInstruction:
    charaNoList = 逐个求值参数得到的整数数组（isDel=true 时先把全部参数收集进列表）
    若 charaNoList.Length == 1:
        VEvaluator.DelCharacter(charaNoList[0])
    否则:
        VEvaluator.DelCharacter(charaNoList)

DelCharacter(charaNo):                     # 单参数版
    若 charaNo < 0 或 charaNo >= CharacterList.Count:
        抛出 CodeEE（"删除角色的登录编号越界"）
    CharacterList[charaNo].Dispose()
    CharacterList.RemoveAt(charaNo)        # 后续角色登录编号整体前移

DelCharacter(charaNoList):                 # 多参数版
    DelList = []
    对每个 charaNo:
        若越界: 抛出 CodeEE
        chara = CharacterList[charaNo]
        若 DelList 已包含 chara: 抛出 CodeEE（"重复删除同一角色"）
        DelList.Add(chara); chara.Dispose()
    对 DelList 中每个 chara:
        CharacterList.Remove(chara)        # 按对象移除，避免逐个删除导致下标偏移
```

## 备注

- 与 `ADDCHARA`、`ADDSPCHARA` 共用一个指令类，靠构造参数 `isDel` 区分删除分支；本命令 `isDel=true, isSp=false`，因此不会走 SP 角色的配置检查分支。
- 多参数版的越界错误消息中把整个 `charaNoList` 数组 `ToString()` 拼进了报错文本（源码 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1092` 的 `string.Format(trerror.OoRDelChara.Text, charaNoList.ToString())`），实际显示的是类型名（.NET 中为 `System.Int64[]`）而非越界编号——这是源码实现上的一个小瑕疵，文档未提及。单参数版（`:1081`）拼的是 `charaNo.ToString()`，编号正确。
- 文档（ecd）只说「参数是登录编号，可一次删多个」，未提越界与重复删除的报错行为，此处以源码为准补充。
