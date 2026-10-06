# GETMEMORYUSAGE

- **类别**：EE 扩展命令（式中函数）
- **签名**：
  - `GETMEMORYUSAGE()`
- **文档来源**：EM+EE 在线文档「GETMEMORYUSAGE」（evilmask.gitlab.io/emuera.em.doc）；`EmueraEE_readme.txt`「GETMEMORYUSAGE()」条目；`ecd/Command.md` 未收录；zh 套件未收录。

## 语义

返回当前正在运行的 Emuera 进程的内存使用量，单位为 byte（整型）。取值来自进程的 WorkingSet（工作集），因此与任务管理器显示的数值会有差异。

常与 `CLEARMEMORY()`（执行 GC 并返回释放的内存量）配合使用，用于观察大量 `ADDVOIDCHARA`、读档、`RESETDATA` 等操作前后的内存变化。无副作用、无参数，可在任意表达式中使用。

## 用法

### `GETMEMORYUSAGE()`
- 无参数；返回整型（当前内存使用量，byte）。
```erb
@SYSTEM_TITLE
PRINTFORMW 当前的内存使用量为 {GETMEMORYUSAGE()/1024/1024} MB
REPEAT 10000
ADDVOIDCHARA
REND
PRINTFORMW 运行 10000 次 ADDVOIDCHARA 之后的内存使用量为 {GETMEMORYUSAGE()/1024/1024} MB
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:325`（`["GETMEMORYUSAGE"] = new GetUsingMemoryMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7277`（`GetUsingMemoryMethod`，`#region EE_GETMEMORYUSAGE`）

```text
class GetUsingMemoryMethod : FunctionMethod
    构造: ReturnType = long; argumentTypeArray = []; CanRestructure = false
    GetIntValue(exm, arguments):
        using (memory = Process.GetCurrentProcess()):
            返回 memory.WorkingSet64   # 当前进程工作集字节数
```

## 备注

- EE v14（EE changelog：`関数追加：GETMEMORYUSAGE, CLEARMEMORY`）加入，EM+EE 发行版独有；本仓库已实现。
- 文档明示「WorkingSet 的值，与任务管理器的数值有差异」；实现与之完全一致，无冲突。
- 在线文档注明「命令/行内函数两种写法均有效」，实现上它只以式中函数（FunctionMethod）形式注册，命令形式依赖 EM+EE 对返回值可丢弃的放行；本仓库仅支持式中函数形式。
