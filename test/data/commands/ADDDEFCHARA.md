# ADDDEFCHARA

- **类别**：命令
- **签名**：
  - `ADDDEFCHARA`
- **文档来源**：`ecd/docs/translation/Command.md`「角色操作·引用」小节；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

执行游戏开始时的系统性角色添加处理。会添加 `chara0*.csv` 中定义的角色，以及 `gamebase.csv` 的 `最初からいるキャラ` 指定的初始角色（`Runtime/Script/Data/GameBase.cs:126`；`GameBase.DefaultCharacter`，未指定时为 -1）。

`ADDCHARA 0` 是查找并添加角色编号（番号）为 0 的角色，而 `ADDDEFCHARA` 是按 CSV 文件编号（`chara0*.csv` 的编号）逐个添加。如果对应的 CSV 不存在，就与 `ADDVOIDCHARA` 一样创建空角色。

这是为了重现 Eramaker 初始化处理的指令，只能在 `@SYSTEM_TITLE` 函数中使用，在其他位置使用会报错。

## 用法

### `ADDDEFCHARA`
- 无参数。
```erb
@SYSTEM_TITLE
ADDDEFCHARA
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:213`（`argb[FunctionArgType.VOID]`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:279`（`Runtime/Script/Process.ScriptProc.cs` 中 `case FunctionCode.ADDDEFCHARA` 分支）；实际添加逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1061`（`AddCharacterFromCsvNo`）

```text
case ADDDEFCHARA:
    若 func.ParentLabelLine != null 且 函数名 != "SYSTEM_TITLE":
        抛出 CodeEE（CanNotUseOutsideSystemtitle，"只能在@SYSTEM_TITLE中使用"）
    vEvaluator.AddCharacterFromCsvNo(0)          # 添加 chara00.csv（CSV 编号 0）
    若 GlobalStatic.GameBaseData.DefaultCharacter > 0:
        vEvaluator.AddCharacterFromCsvNo(DefaultCharacter)  # 添加 gamebase.csv 指定的初始角色

AddCharacterFromCsvNo(CsvNo):
    tmpl = constant.GetCharacterTemplateFromCsvNo(CsvNo)
    若 tmpl == null:
        tmpl = constant.GetPseudoChara()         # CSV 不存在 → 回退为空角色（同 ADDVOIDCHARA）
    chara = 新建 CharacterData(constant, tmpl, varData)
    varData.CharacterList.Add(chara)
```

## 备注

- 文档称「会添加 `chara0*.csv` 中定义的角色」，源码只调用两次 `AddCharacterFromCsvNo`（CSV 编号 0 与 gamebase 指定的默认角色）。已核实 `GetCharacterTemplateFromCsvNo` 的语义：它按 **CSV 文件编号**（`csvNo` 取自文件名 `charaNN.csv` 的 `NN`）在按角色番号排序的模板表中查找，并只返回第一个命中的模板，因此 `ADDDEFCHARA` 每次最多添加两个角色——`chara00.csv` 中番号最小的角色，以及 `chara{DefaultCharacter}.csv` 中番号最小的角色（`DefaultCharacter <= 0` 时只加前者），而不是「chara0*.csv 的全部角色」。文档与源码粒度存在差异，如实记录。
- 本指令是 switch 分发型（走 `Runtime/Script/Process.ScriptProc.cs` 的 switch-case），不是独立指令类。
- zh 套件未收录本命令。
