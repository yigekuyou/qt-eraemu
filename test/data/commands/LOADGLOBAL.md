# LOADGLOBAL

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令
- **签名**：
  - `LOADGLOBAL`
- **文档来源**：`ecd/docs/translation/Command.md`「游戏存档的操作」LOADGLOBAL 小节；zh 套件 `zh/Variable.md`（「`LOADGLOBAL` 指令允许你从 `Global.sav` 加载 `GLOBAL` 和 `GLOBALS`……推荐与 `@EVENTFIRST` 和 `@EVENTLOAD` 一起使用」）及变量表可交叉核对。

## 语义

从 `global.sav` 读取全局变量 `GLOBAL`（整数）与 `GLOBALS`（字符串）。

- 读取失败**不会出错**：成功时把 `1`、失败时把 `0` 赋给 `RESULT`。
- 与普通存档一样，`gamebase.csv` 中设置的代码、版本不合适的文件无法读取（返回 0）。
- 若 `ERH` 头文件中定义了带有 `GLOBAL`、`SAVEDATA` 标志的扩展变量，也会一并读取（与 `SAVEGLOBAL` 对应）。
- 推荐在 `@EVENTFIRST` 与 `@EVENTLOAD` 中调用，把全局数据读入内存。

## 用法

### `LOADGLOBAL`
- 无参数；结果写入 `RESULT`（1 = 成功，0 = 失败）。
```erb
@EVENTLOAD
LOADGLOBAL
IF RESULT == 0
  PRINTL 全局数据读取失败（初次启动或文件不匹配）
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:232`（`new LOADGLOBAL_Instruction()`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1847`（`LOADGLOBAL_Instruction`）；核心在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:2286`（`LoadGlobal`）

```text
DoInstruction(exm, func, state):
    若 exm.VEvaluator.LoadGlobal():
        exm.VEvaluator.RESULT = 1
    否则:
        exm.VEvaluator.RESULT = 0

LoadGlobal():
    filepath = getSaveDataPathG()          # global.sav 的路径
    若 文件不存在: 返回 false
    varData.RemoveEMGlobalData()           # 先清掉 EM 扩展的全局数据（DIM GLOBAL SAVEDATA 等）
    打开 global.sav：
        bReader = EraBinaryDataReader.CreateReader(fs)   # 二进制格式（现行）
        若 bReader != null:
            若 ReadFileType() != Global: 返回 false        # 文件类型不对
            若 gamebase.UniqueCode 与文件不符: 返回 false   # gamebase 代码不同
            version = 读入版本号
            若 gamebase.CheckVersion(version) 失败: 返回 false
            读入 saveMes（忽略）
            VariableData.LoadFromStreamBinary(bReader)     # 读取 GLOBAL/GLOBALS 等
            若 未到 EOF（EM 私家版扩展流）:
                VariableData.LoadFromStreamBinary(bReader)  # 读取 ERH 中 GLOBAL+SAVEDATA 变量
        否则（旧文本格式 EraDataReader 兼容路径）:
            校验代码、版本
            varData.LoadGlobalFromStream(reader)
            若 reader.SeekEmuStart()（1808 扩展段存在）:
                varData.LoadGlobalFromStream1808(reader)
    成功到达末尾 → 返回 true
```

## 备注

- 文档「读取失败也不会出错……赋值 `RESULT`」与源码一致：所有失败路径（文件不存在、类型不符、代码/版本不符）都静默返回 false → `RESULT = 0`。
- 文档「`ERH` 文件中定义了带有 `GLOBAL` 和 `SAVEDATA` 标志的变量，也会一并保存/读取」对应二进制路径末尾的第二个 `LoadFromStreamBinary`（EM 扩展流）与开头的 `RemoveEMGlobalData`。
- zh `Variable.md` 的说法「从 `Global.sav` 加载 `GLOBAL` 和 `GLOBAL(S)`」与 ecd 一致；zh 还补充推荐使用时机（`@EVENTFIRST`/`@EVENTLOAD`），ecd 该小节未写，属于 zh 的补充信息。
- 本仓库实现保留了旧版文本格式（`EraDataReader`）的兼容读取分支，文档未提及。
