# GETDOINGFUNCTION

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（本仓库中实现为式中函数）
- **签名**：
  - `GETDOINGFUNCTION()`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・GETDOINGFUNCTION」。ecd 套件与 zh 套件均未收录本命令。

## 语义

返回当前正在执行的函数名。与调试模式下可用的 `__FUNCTION__` 宏同义，可用于在非调试模式下输出日志来源等用途。系统待机中（不在任何用户函数内）调用时返回空字符串，不报错。

## 用法

### `GETDOINGFUNCTION()`
```erb
@LOG, Texts
PRINTFORML [{GETDOINGFUNCTION()}] 日志输出
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:334`（`["GETDOINGFUNCTION"] = new GetDoingFunctionMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7405`（`GetDoingFunctionMethod`，`#region EE_GETDOINGFUNCTION`；`GetStrValue` 在 7414 行）

```text
构造: ReturnType = string; argumentTypeArray = []（无参数）
      CanRestructure = true

GetStrValue(exm, arguments):
    line = exm.Process.GetScaningLine()          # 解释器当前正在扫的行
    if line == null 或 line.ParentLabelLine == null:
        return ""                                # 系统待机中的调试模式调用等场景
    return line.ParentLabelLine.LabelName        # 所属函数标签名
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准，两者一致（「デバッグモード使用時の__FUNCTION__と同義」）。
- 返回的是函数的标签名（`@` 后的名字，不含参数部分）；通过事件调用链调用时同样返回实际执行中的函数。
- `CanRestructure = true`：无参数且结果只取决于执行位置，表达式重构时常量折叠为调用当时的函数名——在结果被缓存的表达式上下文中语义可能与直觉不符，使用时留意。
