# EXISTFUNCTION

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（本仓库中实现为式中函数）
- **签名**：
  - `EXISTFUNCTION("<函数名>")`
  - `EXISTFUNCTION("<函数名>", <数值表达式>)`（EE 实现扩展的第 2 参数，readme 未记载）
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・EXISTFUNCTION("関数名")」；英文版 `EmueraEE_readme (English).txt`「EXISTFUNCTION("function name")」；`EmueraEE_changelog.txt`（EEv6 追加）。ecd 套件与 zh 套件均未收录本命令。

## 语义

判断指定名称的函数（ERB 中 `@` 开头定义的函数）是否存在，返回值：

- `0`：函数不存在，或目标是系统函数／系统内置式中函数（这些不在用户函数表中，查不到即返回 0）。
- `1`：存在的普通函数（非式中函数）。
- `2`：存在的数值型式中函数（`@FUNCTION` 带 `#FUNCTION`）。
- `3`：存在的字符串型式中函数（`#FUNCTIONS`）。

主要用途是在 `TRYC`～`CATCH` 之外做函数存在性判断，配合 `TRYCALLF`/`TRYCALLFORMF`（它们调用失败不报错但也不返回值）实现「有则调用」的逻辑。

第 2 参数为本仓库源码中的扩展：非 0 时强制做大小写不敏感的全表扫描匹配（不受配置的大小写比较设置影响）；省略或为 0 时按配置的大小写设置直接查表。

## 用法

### `EXISTFUNCTION("<函数名>")`
```erb
IF EXISTFUNCTION("MY_CHECK_FUNC") == 1
    CALL MY_CHECK_FUNC
ELSEIF EXISTFUNCTION("MY_CHECK_F") == 2
    PRINTV MY_CHECK_F()
ENDIF
```

### `EXISTFUNCTION("<函数名>", <忽略大小写>)`
```erb
;即使配置开启了大小写敏感，也按不区分大小写查找
PRINTVL EXISTFUNCTION("my_check_func", 1)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:314`（`["EXISTFUNCTION"] = new ExistFunctionMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7218`（`ExistFunctionMethod`，`#region EE_EXISTFUNCTION`；`GetIntValue` 在 7229 行）

```text
构造: ReturnType = 整数
      argumentTypeArrayEx = [ [string, int], OmitStart = 1 ]   # 第2参数可省略
      CanRestructure = false

GetIntValue(exm, arguments):
    functionname = arguments[0].GetStrValue(exm)
    if arguments.Count == 1 或 arguments[1].GetIntValue(exm) == 0:
        # 快速路径：大小写规则跟随配置
        if Config.StringComparison == OrdinalIgnoreCase:
            func = LabelDictionary.GetNonEventLabel(functionname.ToUpper())
        else:
            func = LabelDictionary.GetNonEventLabel(functionname)
        if func == null: return 0
        if func.IsMethod:                      # 式中函数
            if func.MethodType == string: return 3
            else if func.MethodType == long: return 2
        return 1                               # 普通函数
    else:
        # 第2参数非 0：强制大小写不敏感的全表扫描
        for funcname in LabelDictionary.NoneventKeys:
            if funcname.ToUpper() == functionname.ToUpper():
                func = LabelDictionary.GetNonEventLabel(funcname)
                if func.IsMethod:
                    if func.MethodType == string: return 3
                    else if func.MethodType == long: return 2
                return 1
        return 0
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt（含英文版）为准，实现以本仓库 C# 源码为准。
- readme「システム関数やシステム組み込み式中関数は0を返す」与源码一致：系统函数不在 `LabelDictionary` 的非事件函数表中，`GetNonEventLabel` 返回 null → 0。
- readme 未记载第 2 参数（大小写不敏感扫描开关），该参数仅见于本仓库源码，属于实现超前于文档的扩展。
- 事件函数（`@EVENTxxx` 等）存于事件表而非 `NoneventKeys`，按此实现同样返回 0。
