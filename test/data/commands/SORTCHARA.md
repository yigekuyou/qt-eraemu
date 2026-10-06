# SORTCHARA

- **类别**：命令
- **签名**：SORTCHARA `<角色变量>` {, `<FORWARD or BACK>`}
- **文档来源**：`ecd/docs/translation/Command.md`（`### SORTCHARA <角色变量> {, <FORWARD or BACK>}` 小节）；Era-Chinese-Documentation 未收录本命令

## 语义

按指定变量（排序键）对角色列表排序。排序键可以是 `NAME` 这样的字符串变量、`NO` 这样的数值变量，或 `CFLAG` 这样的数值/字符串数组变量。`FORWARD` 为升序、`BACK` 为降序，省略时升序。

`<角色变量>` 可整体省略，省略时按角色编号 `NO:XX` 升序排序。`MASTER` 不参与排序（排序后回到原位）；`TARGET:0`、`ASSI:0` 会自动追随排序结果，无需手动处理（使用 `TARGET:1` 等改造版需自行追随）。即使 `TARGET == -1` 也不会因引用不到键值而出错（排序并不实际经由 `TARGET` 引用变量）。

## 用法

### SORTCHARA（省略排序键）

按 `NO` 升序排序。

```erb
SORTCHARA
```

### SORTCHARA `<角色变量>`

按指定角色变量的值升序排序。数组变量取该键在当前角色上对应下标的值（见实现：1 维键取其第 1 下标项，2 维键按下标 1、2 打包比较）。

```erb
;按 CFLAG 的第 2 号元素升序
SORTCHARA CFLAG:2
;按 NAME 升序
SORTCHARA NAME
```

### SORTCHARA `<角色变量>`, `<FORWARD or BACK>`

`FORWARD` = 升序，`BACK` = 降序。

```erb
;按 NO 降序
SORTCHARA BACK
;按 NAME 降序
SORTCHARA NAME, BACK
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:283`（`new SORTCHARA_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1509`（类 `SORTCHARA_Instruction`）；实际排序在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1205`（`VariableEvaluator.SortChara`）；参数解析在 `Runtime/Script/Statements/ArgumentBuilder.cs:725`（`SP_SORTCHARA_ArgumentBuilder`）

```text
参数解析（SP_SORTCHARA_ArgumentBuilder）:
    默认 sortKey = 系统变量 NO:0，order = ASCENDING
    若无参数 → 直接返回默认值
    若首词是 FORWARD/BACK → BACK 时 order = DESENDING；其后还有内容则告警「参数过多」
    否则解析第 1 参数为变量项；非变量项 → 告警；不是角色数据(IsCharacterData) → 告警
    其后若还有词，必须是 FORWARD/BACK（BACK → 降序），多余内容告警

SORTCHARA_Instruction.DoInstruction:
    sortKey = spSortArg.SortKey
    elem = 0
    若 sortKey 是 1 维数组: elem = sortKey.GetElementInt(1, exm)          // 第 1 下标
    若 sortKey 是 2 维数组: elem = (GetElementInt(1) << 32) + GetElementInt(2)
    exm.VEvaluator.SortChara(sortKey.Identifier, elem, spSortArg.SortOrder, fixMaster=true)

VariableEvaluator.SortChara(sortkey, elem, sortorder, fixMaster):
    若角色数 <= 1 → 直接返回
    若 sortorder == UNDEF → 视为 ASCENDING
    若 sortkey == null → 用系统变量 NO
    记录 master/target/assi 原位置（越界则不记录）
    对每个角色 i：
        CharacterList[i].temp_CurrentOrder = i
        CharacterList[i].SetSortKey(sortkey, elem)      // 预先取好比较键
    若 fixMaster 且存在 MASTER：
        若角色数 <= 2 → 返回（不排序）
        从列表中移除 MASTER
    按 order 升序/降序对 CharacterList 排序（CharacterData.Asc/DescCharacterComparison）
    若 fixMaster 且存在 MASTER：把 MASTER 重新插回原位置
    重新为每个角色编号 temp_CurrentOrder = i
    若 masterChara != null 且 !fixMaster → MASTER = masterChara.temp_CurrentOrder（本命令 fixMaster=true，故不执行）
    若 targetChara != null → TARGET = targetChara.temp_CurrentOrder
    若 assiChara  != null → ASSI  = assiChara.temp_CurrentOrder
```

## 备注

- ecd 文档说「`MASTER` 不参与排序」，与源码 `fixMaster=true` 的行为一致（MASTER 先移出、排序后插回原位）；`TARGET:0`/`ASSI:0` 自动追随也由实现末尾的赋值保证。
- 枚举名 `SortOrder.DESENDING` 是源码中的拼写（应为 DESCENDING），实际语义为降序。
- Era-Chinese-Documentation 套件未收录本命令。
