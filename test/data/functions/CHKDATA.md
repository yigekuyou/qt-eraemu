# CHKDATA

- **类别**：式中函数
- **签名**：`int CHKDATA(int value)`
- **文档来源**：`ecd/Expression.md`（函数目录签名）、`ecd/Command.md`「CHKDATA `<数值表达式>`」小节（位于 SAVEDATA/LOADDATA 系列小节之间）；zh 套件未收录

## 语义

检查第 `value` 号存档文件（`SAVEDATA`/`LOADDATA` 使用的存档）是否存在、能否读取，返回状态码作为函数值，同时产生两个副作用：
- `RESULTS:0` 被赋值为检查结果信息；
- `RESULT:1` 被赋值为存档的数据版本号（源码行为，文档未提及）。

返回值含义（ecd 文档）：
- `0`：该文件可以读取。
- `1`：指定的文件不存在。
- `2`：游戏代码不同（`gamebase.csv` 中「代码」的值不同）。
- `3`：版本不同（`gamebase.csv` 中「版本」的值不同，且不是允许的版本）。
- `4`：存在上述以外问题的文件。

`RESULT:0`（即返回值）为 0 时，`RESULTS:0` 中是存档数据的注释（`@SAVEINFO` 里 `PUTFORM` 写入的字符串，或 `SAVEDATA` 的第 2 参数）；不为 0 时，`RESULTS:0` 中是诸如「存档版本不同」的错误信息。

只有返回 0 时才能用 `LOADDATA` 读取；`LOADDATA` 读取失败会出错终止，因此读取前应先用本函数确认。

## 用法

### int CHKDATA(int value)
- `value`：存档编号（数值表达式）。为负时抛出运行时错误；超过 int 最大值时抛出运行时错误。
```erb
IF CHKDATA(3) == 0
    PRINTFORML 存档 3：{RESULTS:0}（版本 {RESULT:1}）
    LOADDATA 3
ELSE
    PRINTFORML 存档 3 无法读取（{RESULTS:0}）
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:39`（`["CHKDATA"] = new CheckdataMethod(EraSaveFileType.Normal)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2435`（`CheckdataMethod`）；实际检查逻辑转调 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1856`（`CheckData(int, EraSaveFileType)`）→ `:1873`（`CheckDataByFilename`），文件路径由 `getSaveDataPath(saveIndex)` 生成

```text
函数 CHKDATA(target):
    若 target < 0:            抛出 CodeEE（第 1 参数为负）
    若 target > int.MaxValue: 抛出 CodeEE（第 1 参数过大）
    result ← VEvaluator.CheckData((int)target, Normal)
        filename ← getSaveDataPath(target)        ; 普通存档目录 + saveXX.dat
        后续与 CHKCHARADATA 伪代码中 CheckDataByFilename 相同：
            文件不存在 → (State=1, "----")
            eramaker 格式：代码不符 → 2；版本不符 → 3；OK → (0, 注释, 版本)
            Emuera 二进制：文件类型 ≠ Normal → 4；代码不符 → 2；版本不符 → 3
                          OK → (0, 注释, 版本)
            异常 → (4, 错误信息)
    VEvaluator.RESULTS      ← result.DataMes        ; 副作用 1
    VEvaluator.RESULT_ARRAY[1] ← result.Version     ; 副作用 2：RESULT:1 = 版本
    返回 (long)result.State                          ; RESULT:0 语义上的值
```

## 备注

- 本名字只有式中函数形态，没有同名命令（区别于 `VARSIZE`、`OUTPUTLOG` 等）。
- 文档差异：ecd 文档只说结果写入 `RESULT:0` 和 `RESULTS:0`；源码还把存档版本写入 `RESULT:1`（`RESULT_ARRAY[1] = result.Version`），文档未提及。
- 文档把返回值描述为「赋值给 RESULT:0」，实际它是式中函数的返回值，可直接用在表达式里（如 `IF CHKDATA(3) == 0`），赋给 RESULT 只是惯用接收方式。
- eramaker 旧格式存档也能检查；二进制格式会先校验文件类型必须为 Normal，不符按「其他错误(4)」处理。
