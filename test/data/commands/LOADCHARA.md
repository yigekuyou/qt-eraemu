# LOADCHARA

- **类别**：命令
- **签名**：
  - `LOADCHARA <文件名字符串>`
- **文档来源**：**两套文档均未收录本命令的命令小节**（ecd 的 `Command.md`、`ERB_Commands.md` 命令表及 zh 套件均无 LOADCHARA 条目）。以下语义依据源码归纳。

## 语义

从指定的 `.dat` 文件（由 `SAVECHARA` 保存的角色数据文件，存放在存档目录的 chara 子目录）中读取角色，并把它们**追加**到当前角色的末尾。

- 文件不存在、文件类型不对、游戏代码（`gamebase.csv` 的代码）不一致、版本不合适等情况下一律**静默失败**：不报错，不加载任何角色。
- 成功与否通过 `RESULT` 传回：成功为 1，失败为 0。
- 追加的角色编号从当前 `CHARANUM` 开始，已有角色不受影响。

## 用法

### `LOADCHARA <文件名字符串>`
- `<文件名字符串>`：字符串表达式，要读取的角色数据文件名（如 `"CHARA_01.dat"`）。
```erb
LOADCHARA "CHARA_01.dat"
IF RESULT
  PRINTL 角色读取成功
ELSE
  PRINTL 角色读取失败（文件不存在或数据不匹配）
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:383`（`new LOADCHARA_Instruction()`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1935`（`LOADCHARA_Instruction`，参数 builder 为 `STR_EXPRESSION`）；核心在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:2053`（`LoadChara`）

```text
DoInstruction(exm, func, state):
    arg = (ExpressionArgument)func.Argument
    若 arg.IsConst: datFilename = arg.ConstStr
    否则:          datFilename = arg.Term.GetStrValue(exm)
    exm.VEvaluator.LoadChara(datFilename)

LoadChara(savename):
    filepath = getSaveDataPathC(savename)    # 存档目录 chara 子目录下的该文件名
    RESULT = 0                               # 先置失败
    若 文件不存在: 返回（RESULT 保持 0）
    打开文件流，bReader = EraBinaryDataReader.CreateReader(fs)
    若 bReader == null: 返回
    若 bReader.ReadFileType() != CharVar: 返回          # 不是角色存档文件
    若 gamebase.UniqueCode != 文件中的代码: 返回        # gamebase 代码不一致
    version = 读入版本号
    若 gamebase.CheckVersion(version) 失败: 返回        # 版本不允许
    读入 saveMes（忽略）
    loadnum = 读入角色数
    循环 loadnum 次:
        chara = new CharacterData(constant, varData)
        chara.LoadFromStreamBinary(bReader)
        addCharaList.Add(chara)
    varData.CharacterList.AddRange(addCharaList)        # 追加到角色列表末尾
    RESULT = 1                                          # 成功
```

## 备注

- **文档未收录**：ecd/zh 两套文档均无本命令条目，语义完全由源码（`LOADCHARA_Instruction` + `VariableEvaluator.LoadChara`）推导；「与 SAVECHARA 配套」「追加读取」从实现（`CharacterList.AddRange`）可直接确认。
- 与 `LOADGLOBAL` 不同（失败也静默、但用 `RESULT` 1/0 报告），`LOADCHARA` 所有失败路径都静默置 `RESULT = 0`，与 `CHKDATA` 系（LOADDATA/SAVEGAME 系）的显式错误码风格不同。
- 与同组的 `SAVECHARA`（可指定角色编号列表、注释、有范围/重复校验并抛错）相比，`LOADCHARA` 只有一个文件名参数，且不做任何运行期参数校验。
