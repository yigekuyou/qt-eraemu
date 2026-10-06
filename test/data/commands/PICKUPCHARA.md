# PICKUPCHARA

- **类别**：命令
- **签名**：
  - `PICKUPCHARA <目标角色>(, <目标角色>, ....)`
- **文档来源**：`ecd/docs/translation/Command.md`「角色操作」小节（`### PICKUPCHARA`）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

只保留参数中指定的角色（按参数顺序重新排列到角色列表开头），删除其他所有角色。`MASTER:0`、`TARGET:0`、`ASSI:0` 等会自动追随重排后的位置，指令结束后无需手动重新设置。

目标角色指定负值时会出错；但如果把 `MASTER`、`TARGET`、`ASSI` 等变量本身作为目标、而这些变量的内容恰好是负值（未设置状态），则是例外——不会出错，会被忽略。重复指定同一角色没有问题（自动去重）。

## 用法

### `PICKUPCHARA <目标角色>{, <目标角色>, ...}`
- `<目标角色>`：角色登录编号的数值表达式，或 `MASTER`/`TARGET`/`ASSI` 等角色变量（可带索引）。至少需要 1 个参数。普通数值表达式为负或 `>= CHARANUM` 时报错。
```erb
;只保留 0 号与 3 号角色，其余全部删除
PICKUPCHARA 0, 3
;保留 TARGET 与 ASSI 指向的角色（若未设置则被忽略）
PICKUPCHARA TARGET, ASSI
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:315`（`argb[FunctionArgType.INT_ANY]`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:263`（`case FunctionCode.PICKUPCHARA` 分支）；核心算法 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1112`（`PickUpChara`）

```text
case PICKUPCHARA:
    intExpArg = (ExpressionArrayArgument)func.Argument
    charaNum = vEvaluator.CHARANUM
    对第 i 个参数（i 从 0 起）:
        NoList[i] = 求值
        若 该项不是变量项，或其变量不是 MASTER/ASSI/TARGET:
            若 NoList[i] < 0 或 NoList[i] >= charaNum:
                抛出 CodeEE（"PICKUPCHARA 的第 (i+1) 参数越界：<值>"）
        # MASTER/ASSI/TARGET 变量即便内容为负也不检查（会被忽略）
    vEvaluator.PickUpChara(NoList)

PickUpChara(NoList):
    oldTarget/oldAssi/oldMaster = 当前 TARGET/ASSI/MASTER
    TARGET = ASSI = MASTER = -1
    # 去重并丢弃负值
    pickList = NoList 中未出现过且 >= 0 的值，按原顺序
    # 把保留的角色依次交换到列表开头 [0, 1, 2, ...]
    对 i = 0 .. pickList.Count-1:
        若 i != pickList[i]:
            SwapChara(pickList[i], i)            # 交换两个登录位置的角色
            若 pickList 中值 i 的位置 > i:
                把该位置的值更新为 pickList[i]   # 交换后修正后续待交换的编号
        若 TARGET < 0 且 pickList[i] == oldTarget: TARGET = i   # 追随新位置
        若 ASSI  < 0 且 pickList[i] == oldAssi:    ASSI  = i
        若 MASTER < 0 且 pickList[i] == oldMaster: MASTER = i
    # 删除末尾多余的角色
    若 pickList.Count < 角色数:
        对 i = 角色数-1 递减到 pickList.Count:
            DelCharacter(i)
```

## 备注

- 文档「`MASTER`、`TARGET`、`ASSI` 等设为目标而这些变量内容为负时不会出错」：源码检查条件是「该项不是 `VariableTerm` 或其变量代码不是 MASTER/ASSI/TARGET 才做范围检查」（`Runtime/Script/Process.ScriptProc.cs:272`），与文档一致。注意「等」实际上只覆盖这三个变量代码，其他角色变量（如 `CANDIDATE`）不在豁免之列。
- 保留的角色会被物理移动到角色列表开头并按参数顺序排列（`SwapChara`），这一点文档未明说，但「`MASTER` 等自动追随」的行为由此实现。
- zh 套件未收录本命令。
