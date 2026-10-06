# PUTFORM

- **类别**：命令
- **签名**：`PUTFORM <格式字符串表达式>`（与 `PRINTFORM` 相同的格式）
- **文档来源**：`ecd/docs/translation/Command.md` 未收录独立小节，相关说明见其「游戏存档的操作」（`SAVEDATA`／`LOADGAME` 节中提及 PUTFORM）；`Era-Chinese-Documentation/docs/ERB_File_Format.md:1281`（「`PUTFORM` 只能与名为 `@SAVEINFO` 的特殊函数一起使用……」）。

## 语义

只能在 `@SAVEINFO` 函数中使用的命令。用与 `PRINTFORM` 相同的 `{表达式}`／`%变量名%` 格式书写一段字符串，把它追加到存档数据的注释（`SAVEDATA_TEXT`）中，用于描述存档概况（如经过了多少天、角色能力、正在训练谁等）。读取存档信息时（`LOADGAME` 后），这段文字会被放入 `RESULTS:0`。

- `SAVEDATA` 命令不会调用 `@SAVEINFO`，因此不能用 `PUTFORM` 写注释，取而代之的是用 `SAVEDATA` 的第 2 参数指定注释（ecd/Command.md 存档操作节）。
- 实现上是把参数求值后的字符串追加（`+=`）到系统字符串变量 `SAVEDATA_TEXT`；`SAVEDATA_TEXT` 为空（null）时直接赋值。不显示到画面。

## 用法

### `PUTFORM <格式字符串表达式>`
- `<格式字符串表达式>`：与 `PRINTFORM` 相同书写格式的字符串表达式。

```erb
@SAVEINFO
PUTFORM {DAY}日目　主人是{CALLNAME:MASTER}　正在训练{CALLNAME:TARGET}
; 保存后选择该存档时，这段文字会作为注释显示在 LOADGAME 界面
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:217`（`argb[FunctionArgType.FORM_STR_NULLABLE], METHOD_SAFE`，注释「@SAVEINFO関数でのみ使用可能。PRINTFORMと同様の書式でセーブデータに概要をつける。」）
- 实现：`Runtime/Script/Process.ScriptProc.cs:289-298`（switch-case `FunctionCode.PUTFORM`）；`SAVEDATA_TEXT` 即系统字符串变量（`Runtime/Script/Statements/Variable/VariableEvaluator.cs:2561-2565`）；保存流程见 `Runtime/Script/Process.SystemProc.cs:694`（保存时先置为当前时间字符串，再由 `@SAVEINFO` 的 PUTFORM 追加）

```text
term = (ExpressionArgument)func.Argument.Term
str = term.GetStrValue(exm)      ; 已按 PRINTFORM 规则展开 {…} / %…%
若 vEvaluator.SAVEDATA_TEXT != null：
    vEvaluator.SAVEDATA_TEXT += str    ; 追加到既有存档注释之后
否则：
    vEvaluator.SAVEDATA_TEXT = str
```

## 备注

- 源码中 `PUTFORM` 的注册是 `METHOD_SAFE`，但**没有**限制「只能在 `@SAVEINFO` 中使用」的运行时检查——在任何函数里执行都会改写 `SAVEDATA_TEXT`，只是脱离保存流程时没有意义。文档（zh/ERB_File_Format.md）强调「只能与 @SAVEINFO 一起使用」是使用约定而非引擎强制，这一点文档与源码略有出入。
- `zh/Command.md` 未收录。
- 保存时（SystemProc.cs:694）`SAVEDATA_TEXT` 先被置为 `yyyy/MM/dd HH:mm:ss` 形式的当前时间加空格，`@SAVEINFO` 中的 PUTFORM 文字追加在其后。
