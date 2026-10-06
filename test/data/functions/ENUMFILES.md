# ENUMFILES

- **类别**：式中函数（EM 私家版扩展，名字枚举函数族）
- **签名**：int ENUMFILES(str 目录{, str 通配模式{, int 递归{, ref str 输出数组}}})
- **文档来源**：两套中文文档与 EM/EE readme 均未收录本函数（readme 的 ENUM 小节只列了 FUNC/VAR/MACRO 的 9 个），语义据源码

## 语义

枚举目录下的文件，把文件路径写入字符串数组，返回**写入个数**；目录不合法或枚举失败返回 `-1`。

- 与 `ENUMFUNC*/ENUMVAR*/ENUMMACRO*` 不同：本函数枚举的是**文件系统**，实现类也是独立的 `EnumFilesMethod`。
- 参数（全部可选，除第 1 个）：
  1. 目录：字符串表达式；经 `Utils.GetValidPath` 处理——`/` 换成 `\`、删除 `..\`、**绝对路径直接拒绝**（返回 null），最终相对 Emuera.exe 所在目录解析（`Runtime/Utils/EvilMask/Utils.cs:214-225`）。目录不存在 → 返回 `-1`。
  2. 通配模式：默认 `"*"`（`Directory.EnumerateFiles` 的 searchPattern，如 `"*.erb"`、`"*.csv"`）。
  3. 递归标志：省略或 0 → 只搜当前目录；非 0 → 连同子目录（`SearchOption.AllDirectories`）。
  4. 输出数组：一维字符串数组变量；省略时写系统数组 `RESULTS`（`RESULTS:0` 起）。
- 写入的路径是**相对 Emuera.exe 目录的相对路径**（源码对每个结果做 `Path.GetRelativePath(Program.ExeDir, ...)`，`Runtime/Script/Statements/Function/Creator.Method.cs:247`），递归时子目录名也在其中。
- 返回值 = 写入个数 `min(文件数, 目标数组容量)`，**不是文件总数**；超出容量的文件被丢弃（`RESULTS` 默认长度 100，`Runtime/Script/Data/ConstantData.cs:189-190`）。
- 枚举过程中任何异常（权限、无效路径字符等）被 `catch` 吞掉并返回 `-1`（`Runtime/Script/Statements/Function/Creator.Method.cs:251-254`）。
- 顺序取决于文件系统，**不保证有序**（源码未排序）。

## 用法

### int ENUMFILES(str 目录)
```erb
#DIM n
n = ENUMFILES("erb")
PRINTFORML {n}                 ; 写入个数
FOR i, 0, n
	PRINTFORML {RESULTS:i}     ; 相对 Emuera.exe 的路径，如 erb\MAIN.ERB
NEXT
```

### int ENUMFILES(str 目录, str 模式, int 递归, ref str 输出数组)
```erb
#DIMS FILES, 200
#DIM n
n = ENUMFILES("csv", "*.csv", 1, FILES)   ; 含子目录，只取 csv
PRINTFORML {n}
IF n > 0
	PRINTFORML [{FILES:0}]
ENDIF
PRINTFORML {ENUMFILES("no_such_dir")}     ;→ -1
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:235`（`["ENUMFILES"] = new EnumFilesMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:223`（`EnumFilesMethod`）
- 路径校验：`Runtime/Utils/EvilMask/Utils.cs:214`（`GetValidPath`）

```text
构造（Creator.Method.cs:225-232）:
    返回类型 = long
    argumentTypeArrayEx = [{ String, String, Int, RefString1D }, OmitStart = 1]
    ; 只有目录必填；模式/递归/输出数组可省（输出数组须是一维字符串数组变量）
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:233-263）:
    dir = Utils.GetValidPath(args[0] 的字符串值)         ; 拒绝绝对路径，去掉 "..\"，相对 ExeDir
    if dir == null || !Directory.Exists(dir): return -1
    pattern = (args.Count > 1) ? args[1] 的字符串值 : "*"
    option  = (args.Count > 2)
              ? (args[2] == 0 ? TopDirectoryOnly : AllDirectories)
              : TopDirectoryOnly
    try:
        files = Directory.EnumerateFiles(dir, pattern, option).ToArray()
        for i: files[i] = Path.GetRelativePath(Program.ExeDir, files[i])   ; 相对化
    catch:
        return -1
    output = (args.Count == 4) ? args[3] 的字符串数组 : exm.VEvaluator.RESULTS_ARRAY
    ret = Math.Min(files.Length, output.Length)
    Array.Copy(files, output, ret)               ; 超容量部分丢弃
    return ret                                   ; 返回写入个数
```

## 备注

- readme 的 ENUM 小节（`Emuera.EM_readme.txt:64-78`）没有本条目，且该小节称「返回总数」，与本函数（乃至其 9 个同族函数）源码「返回写入数」的行为不同——本函数属后来新增（源码同区域 `#region EM_私家版_追加関数`，`Runtime/Script/Statements/Function/Creator.cs:235`），readme 未同步。
- 与 `EXISTFILE` 同属「按路径访问文件系统」的 EM 私家版函数，路径规则一致（相对 Emuera.exe、绝对路径不可用）。
- 没有文件类型过滤（`*.erb` 需自行写模式）；也不会区分文件/目录（`EnumerateFiles` 只返回文件）。
- 结果数可能受 `RESULTS`（默认 100）限制：枚举大目录时必须显式传足够大的输出数组。
- 本仓库移植版（`src/eraengine/`）未实现本函数族。
