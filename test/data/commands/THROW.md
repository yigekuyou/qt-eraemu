# THROW

- **类别**：命令
- **签名**：THROW `<FORM 格式文本>`
- **文档来源**：`ecd/docs/translation/Command.md`（`### THROW <FORM 格式文本>` 小节）；Era-Chinese-Documentation 未收录本命令

## 语义

强制产生运行时错误（异常），并以参数给出的字符串作为错误信息显示。参数支持 FORM 格式语法（`%表达式%`、`{数值表达式}` 等会先展开），常用于在脚本中自行检测异常状态并带说明地中断执行。

参数可省略（按空字符串报错）。

## 用法

### THROW `<FORM 格式文本>`

- `<FORM 格式文本>`：作为错误信息显示的文本，支持 FORM 语法。
- 行为：立即抛出运行时错误并中断当前执行流程。

```erb
IF TARGET < 0
    THROW 目标角色不存在（TARGET={TARGET}）
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:378`（`argb[FunctionArgType.FORM_STR_NULLABLE]`，METHOD_SAFE | EXTENDED）
- 实现：`Runtime/Script/Process.ScriptProc.cs:751`（`case FunctionCode.THROW`）；参数解析在 `Runtime/Script/Statements/ArgumentBuilder.cs:660`（`FORM_STR_ArgumentBuilder(nullable=true)`）

```text
参数解析（FORM_STR_ArgumentBuilder，nullable）:
    参数为空 → 生成空字符串常量项（""）
    否则 → AnalyseFormattedString 解析 FORM 语法，生成字符串求值项

case THROW:
    // 单行，直接抛出
    throw new CodeEE(((ExpressionArgument)func.Argument).Term.GetStrValue(exm))
    // GetStrValue 求值时完成 FORM 展开，展开结果即为显示的错误信息
```

## 备注

- 源码实现非常直接：等价于以参数字符串为 message 抛出 `CodeEE`（Emuera 的脚本运行时错误），上层错误处理会显示该字符串及行号等信息。
- 文档与源码一致；参数可省略这一点来自 `FORM_STR_NULLABLE`（文档未明说，照实补记）。
- Era-Chinese-Documentation 套件未收录本命令。
