# CHKCHARADATA

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（EE 扩展函数）
- **签名**：`int CHKCHARADATA(str datfilename)`
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 未收录；zh 套件未收录。语义依据源码实现（EE 扩展的变量数据检查函数）。

## 语义

检查指定文件名的角色变量数据文件（角色存档，由 `SAVECHARA` 等写入的 `.dat` 文件）是否存在、能否读取，返回状态码并把详细信息写入 `RESULTS:0`。

返回值含义（与 `CHKDATA` 相同的状态码）：
- `0`：文件可读取。
- `1`：文件不存在。
- `2`：游戏代码不同（gamebase.csv 的代码不一致）。
- `3`：版本不同（gamebase.csv 的版本不一致且非允许版本）。
- `4`：其他问题（文件损坏、文件类型不符等）。

`RESULTS:0` 被赋值为检查结果信息：状态 0 时为保存时写入的注释字符串；状态 1 时为 `"----"`；其他状态时为错误说明（如「游戏不同的存档」「存档版本不同」「存档数据损坏」等）。

注意与 `CHKDATA` 不同：本函数不把数据版本写入 `RESULT:1`。参数是文件名字符串而非存档编号。

## 用法

### int CHKCHARADATA(str datfilename)
- `datfilename`：角色变量数据文件的文件名（字符串表达式）。
```erb
IF CHKCHARADATA("chara.dat") == 0
    PRINTFORML 可以读取：{RESULTS:0}
ELSE
    PRINTFORML 无法读取：{RESULTS:0}
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:58`（`["CHKCHARADATA"] = new CheckdataStrMethod(EraSaveFileType.CharVar)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2465`（`CheckdataStrMethod`）；实际检查逻辑转调 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1839`（`CheckData(string, EraSaveFileType)`）→ `:1873`（`CheckDataByFilename`），文件路径由 `getSaveDataPathC(datfilename)` 生成

```text
函数 CHKCHARADATA(datFilename):
    result ← VEvaluator.CheckData(datFilename, CharVar)
        filename ← getSaveDataPathC(datFilename)   ; 角色变量数据目录 + 文件名
        若文件 filename 不存在:
            返回 (State=1, DataMes="----", Version=0)
        打开文件:
            若为 eramaker 旧格式:
                校验游戏代码，不一致 → (State=2, DataMes="游戏不同")
                校验版本，不允许     → (State=3, DataMes="版本不同", Version=版本)
                否则 → (State=0, DataMes=注释字符串, Version=版本)
            若为 Emuera 二进制格式:
                读文件类型，不等于 CharVar → (State=4, DataMes="存档数据损坏")
                校验游戏代码，不一致 → (State=2, ...)
                校验版本，不允许     → (State=3, ...)
                否则 → (State=0, DataMes=注释字符串, Version=版本)
            读写出错/异常 → (State=4, DataMes=错误信息)
    VEvaluator.RESULTS ← result.DataMes
    返回 (long)result.State
```

## 备注

- 三套文档（ecd、zh）均未收录本函数，属 EE 扩展；以上语义完全来自源码。
- 与 `CHKDATA`（`CheckdataMethod`）共用同一套 `EraDataState` 状态码，但 `CheckdataStrMethod.GetIntValue` 只写 `RESULTS`，不写 `RESULT_ARRAY[1]`（版本号），这是与 `CHKDATA` 的实现差异。
- 同族函数：`CHKVARDATA`（Var 类文件）、`CHKGLOBALDATA`（global.sav）在本仓库中被注释掉未注册（见各自文档）；本函数是三者中唯一实际注册可用的。
