# CHKVARDATA

- **类别**：式中函数（EE 计划扩展，本仓库未注册）
- **签名**：（未正式发布；按源码实现类推断为 `int CHKVARDATA(str datfilename)`）
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 未收录；zh 套件未收录。语义依据源码中已实现但未注册的代码。

## 语义

按设计意图，本函数用于检查变量数据文件（Var 类 `.dat` 文件，即计划中 `SAVEVARDATA` 之类指令保存的变量存档）是否存在、能否读取，返回与 `CHKDATA` 相同的状态码，并把结果信息写入 `RESULTS:0`：
- `0`：文件可读取；`1`：文件不存在；`2`：游戏代码不同；`3`：版本不同；`4`：其他问题。

**本仓库（emuera.em EE 版）中该函数未被注册**：注册处一行被注释掉（`//TODO:1810`），运行时调用 `CHKVARDATA(...)` 会因函数未定义而解析失败。

## 用法

### （未注册，无可用用法）
按源码实现，参数为数据文件名的字符串表达式。
```erb
; 本仓库当前会报“函数不存在”类错误
;IF CHKVARDATA("myvar.dat") == 0
;    PRINTFORML 可以读取：{RESULTS:0}
;ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:57` — **被注释**（`//methodList["CHKVARDATA"] = new CheckdataStrMethod(EraSaveFileType.Var);`）
- 实现：类本体与 `CHKCHARADATA` 共用 `Runtime/Script/Statements/Function/Creator.Method.cs:2465`（`CheckdataStrMethod`）；检查逻辑转调 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1839`（`CheckData(string, EraSaveFileType)`），Var 分支下路径为 `getSaveDataPathV(datfilename)`

```text
函数 CHKVARDATA(datFilename):      ; 若注册，行为如下
    result ← VEvaluator.CheckData(datFilename, Var)
        filename ← getSaveDataPathV(datFilename)   ; 变量数据目录 + 文件名
        文件不存在 → (State=1, "----")
        Emuera 二进制：文件类型 ≠ Var → (State=4, 损坏)
                      代码不符 → 2；版本不符 → 3；OK → (0, 注释, 版本)
    VEvaluator.RESULTS ← result.DataMes            ; 不写 RESULT:1
    返回 (long)result.State
```

## 备注

- 三套文档均未收录；与 `CHKGLOBALDATA`、`FIND_VARDATA` 同属 `//TODO:1810` 注释块，官方「作ったけど保留」（做了但搁置，见 `Runtime/Script/Statements/Function/Creator.Method.cs` 中 `GetRefMethod` 处的同款注释）。
- 与 `CHKCHARADATA` 使用同一个 `CheckdataStrMethod` 类，仅 `EraSaveFileType` 不同（Var / CharVar）；同样不写 `RESULT:1`（版本）。
- 源码注释原文：「ファイル名をstringで指定する版・CHKVARDATAとCHKCHARADATAはこっちに分類」。
