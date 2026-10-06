# SAVECHARA

- **类别**：命令
- **签名**：SAVECHARA `<字符串表达式>`, `<字符串表达式>`, `<数值>`, `<数值>`(, `<数值>`...)
- **文档来源**：`ecd/docs/translation/Command.md` 未收录（仅 `Error_Index.md` 列出其错误消息）；Era-Chinese-Documentation 未收录该命令

## 语义

把指定的若干名已登录角色的角色变量保存到以第 1 参数命名的 `*.dat` 文件中（角色存档，区别于 `SAVEDATA` 的游戏进度存档）。第 2 参数为保存时写入文件的注释字符串；其后依次列出要保存的角色编号。之后可用 `LOADCHARA` 读回。

限制与错误：
- 所有角色编号必须为正数且不超过 int.MaxValue，否则解析期报错；
- 角色编号必须小于当前登录角色数 `CHARANUM`，否则运行时抛 CodeEE（超范围）；
- 角色编号不得重复，解析期与运行时都会查重并报错。

## 用法

### SAVECHARA `<文件名(字符串式)>`, `<注释(字符串式)>`, `<角色编号>`, `<角色编号>`(, ...)
- `<文件名>`：保存文件名（自动补 `.dat`，保存到角色存档目录）。
- `<注释>`：写入存档的说明文字。
- `<角色编号>`...：一个或多个已登录角色编号（0 起）。

```erb
;把角色 0 与角色 2 保存到 chara_backup.dat
SAVECHARA "chara_backup", "重要角色的备份", 0, 2
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:382` → `new SAVECHARA_Instruction()`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:270`
- 参数解析：`Runtime/Script/Statements/ArgumentBuilder.cs:2157`（`SP_SAVECHARA_ArgumentBuilder`，`Runtime/Script/Statements/FunctionArgType.cs:70` 注明签名 `<数値>, <文字列式>, <数値>（, <数値>...）`——按实现实际为「文件名字符串, 注释字符串, 角色编号...」）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1903`（`SAVECHARA_Instruction`）；核心逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:2014`（`VariableEvaluator.SaveChara`）

```text
解析期（SP_SAVECHARA_ArgumentBuilder.CreateArgument）:
    参数类型序列 = [string, string, long]，最少 3 个参数，其后可任意多个
    for i = 2 .. 末尾:                       // 从第 3 个参数起是角色编号
        if 第 i 项是常量整数:
            if iValue < 0:      warn("角色编号必须为正数") 并失败
            if iValue > int.MaxValue: warn("超过 int32") 并失败
            for j = i+1 .. 末尾:
                if 常量且 iValue == 第 j 项的值: warn("变量保存目标重复") 并失败

类 SAVECHARA_Instruction:
  标志 = METHOD_SAFE | EXTENDED
  DoInstruction(exm, func, state):
    terms = ((ExpressionArrayArgument)func.Argument).TermList
    datFilename = terms[0].GetStrValue(exm)
    savMes      = terms[1].GetStrValue(exm)
    savCharaList = new int[terms.Length - 2]
    charanum = (int)exm.VEvaluator.CHARANUM
    for i = 0 .. savCharaList.Length-1:
        v = terms[i+2].GetIntValue(exm)
        savCharaList[i] = toUInt32inArg(v, "SAVECHARA", i+3)   // 负数或超过 int.MaxValue 抛 CodeEE
        if savCharaList[i] >= charanum:
            throw CodeEE("SAVECHARA 的第 (i+3) 个参数超出已登录角色范围")
        for j = 0 .. i-1:
            if savCharaList[i] == savCharaList[j]:
                throw CodeEE("角色编号重复")
    exm.VEvaluator.SaveChara(datFilename, savMes, savCharaList)

VariableEvaluator.SaveChara(savename, savMes, charas):
    CreateDatFolder()                       // 确保存档目录存在
    CheckDatFilename(savename)              // 检查文件名合法性（自动补 .dat）
    filepath = getSaveDataPathC(savename)
    fs = 打开 filepath（Create/Write）
    bWriter = EraBinaryDataWriter(fs)
    bWriter.WriteHeader()
    bWriter.WriteFileType(CharVar)          // 文件类型：角色变量存档
    bWriter.WriteInt64(gamebase.ScriptUniqueCode)   // 脚本唯一码（用于校验）
    bWriter.WriteInt64(gamebase.ScriptVersion)      // 脚本版本
    bWriter.WriteString(savMes)                     // 注释
    bWriter.WriteInt64(charas.Length)               // 保存的角色数
    for i = 0 .. charas.Length-1:
        varData.CharacterList[charas[i]].SaveToStreamBinary(bWriter, varData)   // 逐个角色序列化
    bWriter.WriteEOF()
    finally: 关闭 writer/流
```

## 备注

- 本命令在两套文档的 Command.md 中均未单列，语义只能从源码与错误索引（`ecd/Error_Index.md:463` 的「SAVECHARAの第…」错误）确认；上面签名一栏以源码 `Runtime/Script/Statements/FunctionArgType.cs:70` 与实现为准（注意该注释写的「<数値>, <文字列式>, <数値>...」顺序与实现不符，实现是先两个字符串参数再数值列表）。
- 与 `SAVEDATA`（保存整个游戏变量）不同，`SAVECHARA` 只保存指定角色的变量，且文件类型标记为 `CharVar`。
- Era-Chinese-Documentation 未收录本命令。
