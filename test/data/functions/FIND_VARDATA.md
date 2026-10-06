# FIND_VARDATA

- **类别**：式中函数（**本仓库未注册，不可调用**）
- **签名**：int FIND_VARDATA(str pattern = "*")（按实现类推断）
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 未收录；zh 套件未收录。语义依据源码中的实现类（与 FIND_CHARADATA 共用 `FindFilesMethod`）。

## 语义

设计上与 `FIND_CHARADATA` 相同，区别仅在于检索的是变量存档文件 `var_<pattern>.dat`：把匹配到的存档名（去掉 `var_` 前缀与 `.dat` 后缀）写入 `RESULTS:0` 起的数组，返回匹配个数。

**但本仓库中该函数的注册行被注释掉，实际并未注册，脚本中调用会得到「函数不存在」错误。** 这一点与原版 EmueraEE 不同，移植 ERA 脚本时需注意。

## 用法

### int FIND_VARDATA(pattern)（不可用）
- pattern：通配符模式，匹配 `var_<pattern>.dat`。省略时为 `"*"`。
- 若被启用，行为同 FIND_CHARADATA（把 `chara_` 换成 `var_`）。
```erb
; 本仓库中以下调用会报错（未注册）
count = FIND_VARDATA()
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:60` — **该行被注释**：
  `//methodList["FIND_VARDATA"] = new FindFilesMethod(EraSaveFileType.Var);`
  全仓库无其他注册点（已 grep 验证），即本函数未注册。
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2488`（`FindFilesMethod`，与 `FIND_CHARADATA` 共用，由构造参数 `EraSaveFileType.Var` 区分）；枚举核心 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1800`（`GetDatFiles`）

```text
; 若注册被恢复，语义如下（伪代码）：
FindFilesMethod(type = EraSaveFileType.Var).GetIntValue(exm, args):
    pattern = args.Count > 0 ? args[0] : "*"
    filepathes = GetDatFiles(charadat: false, pattern)
        ; searchPattern = "var_" + pattern + ".dat"
        ; filename = 去掉前 4 字符（"var_"）和 ".dat"
    把 filepathes 复制进 RESULTS（超出长度截断）
    返回 filepathes.Count
```

## 备注

- **文档与源码的关键差异**：source_index.md 将本函数标注为「register: Creator.cs:60」，但该行实际是注释，函数未注册，调用必然失败。
- 原版 Emuera 1.810 前后有 `//TODO:1810` 注释群，`CHKVARDATA`、`CHKGLOBALDATA` 的注册同样被注释，本函数属于同一批未启用功能。
- 若需枚举变量存档，目前只能用 `FIND_CHARADATA` 无法替代；可直接用脚本层文件函数或等待上游恢复注册。
