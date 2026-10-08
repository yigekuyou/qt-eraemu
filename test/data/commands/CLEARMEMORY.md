# CLEARMEMORY

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（式中函数）
- **签名**：
  - `CLEARMEMORY()`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・CLEARMEMORY()」「・GETMEMORYUSAGE()」；`EmueraEE_changelog.txt`（EMv?：「関数追加：`GETMEMORYUSAGE`, `CLEARMEMORY`」）。ecd 套件与 zh 套件均未收录本命令。

## 语义

强制进行垃圾回收（GC.Collect），释放内存，返回释放掉的内存量（字节）。它是在式中使用的函数（返回整数，无参数），不是普通语句。

- 适合在 `DELCHARA`、加载存档、`RESETDATA` 等删除大量数据的时机调用；在其他时机调用基本没有效果。
- 处理较重，不宜滥用。
- 与 `GETMEMORYUSAGE()`（返回当前工作集字节数）配套使用。
- README 注明返回值基于 Windows 工作集（Working Set），与任务管理器显示的数值可能有差异。

## 用法

### `CLEARMEMORY()`
```erb
RESETDATA
PRINTFORML 释放了 {CLEARMEMORY()} 字节内存。
;也可先记录再比较：
;before = GETMEMORYUSAGE()
;…删除数据…
;result = CLEARMEMORY()
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:326`（`["CLEARMEMORY"] = new ClearMemoryMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7294-7316`（`ClearMemoryMethod : FunctionMethod`，`#region EE_CLEARMEMORY`）

```text
构造: ReturnType = long; argumentTypeArray = []（无参）; CanRestructure = false

GetIntValue(exm, arguments):
    destmemory = Process.GetCurrentProcess()
    destmemorysize = destmemory.WorkingSet64     # 回收前的工作集（字节）
    GC.Collect()                                 # 强制垃圾回收
    memory = Process.GetCurrentProcess()
    return destmemorysize - memory.WorkingSet64  # 回收前后工作集之差
```

## 备注

- 本命令在本仓库 C# 源码中有实现（`ClearMemoryMethod`），README 所述「メモリを解放する。解放されたメモリ量（byte）を返す」与源码语义一致：源码实际只做一次 `GC.Collect()`，返回值是托管 GC 前后的 WorkingSet64 之差，因此返回值可能为 0 甚至为负（工作集受 OS 影响，不保证单调减小），README 未提及这一点。
- ecd 与 zh 两套文档均未收录本命令。
- 注意：`emuera_ee_cmds.txt` 把它列在命令表中，但在实现上它是式中函数（`Runtime/Script/Statements/Function/Creator.cs` 的方法字典注册，`RETURN ... CLEARMEMORY()` / `PRINTFORML {CLEARMEMORY()}` 等式中用法），不能作为独立语句写。
