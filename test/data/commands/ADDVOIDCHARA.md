# ADDVOIDCHARA

- **类别**：命令
- **签名**：
  - `ADDVOIDCHARA`
- **文档来源**：`ecd/docs/translation/Command.md`「角色操作·引用」小节；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

不依赖 CSV 地添加角色。用 `ADDVOIDCHARA` 添加的角色，其所有变量都被赋值为 0 或 `""`（空字符串）。新角色追加到角色列表末尾，`CHARANUM` 增加 1。无参数、无返回值。

## 用法

### `ADDVOIDCHARA`
- 无参数。
```erb
;添加一个空角色（其 NO 等变量全为 0/空串，之后再手动设置）
ADDVOIDCHARA
PRINTFORML CHARANUM = {CHARANUM}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:214`（`new ADDVOIDCHARA_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1446`（`ADDVOIDCHARA_Instruction`）；实际逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1071`（`AddPseudoCharacter`）

```text
指令类 ADDVOIDCHARA_Instruction:
    VEvaluator.AddPseudoCharacter()

AddPseudoCharacter():
    tmpl = constant.GetPseudoChara()      # 取"伪角色模板"（全 0/空串）
    chara = 新建 CharacterData(constant, tmpl, varData)
    varData.CharacterList.Add(chara)      # 追加到角色列表末尾
```

## 备注

- 与 `ADDDEFCHARA` 共用同一个「空角色」模板：`ADDDEFCHARA` 在对应 CSV 不存在时也回退到这个空角色模板（`AddCharacterFromCsvNo` 中 `tmpl == null` 时取 `GetPseudoChara()`，与 `AddPseudoCharacter` 取的是同一个模板）。
- zh 套件未收录本命令。
