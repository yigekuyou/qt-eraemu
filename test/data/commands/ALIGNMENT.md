# ALIGNMENT

- **类别**：命令
- **签名**：
  - `ALIGNMENT <LEFT or CENTER or RIGHT>`
- **文档来源**：`ecd/docs/translation/Command.md`「颜色/字体/对齐等显示样式」区块下「ALIGNMENT」小节；`Era-Chinese-Documentation` 套件无专节（`HTML_PRINT.md` 中提及 `HTML_PRINT` 不受 `ALIGNMENT` 影响，可佐证）。

## 语义

改变当前文字的对齐方式。有效参数为 `LEFT`、`CENTER`、`RIGHT` 的文本。默认文字显示为左端对齐（即 `ALIGNMENT LEFT`）。

居中对齐 `ALIGNMENT CENTER` 可用于制作文字居中的标题画面。`ALIGNMENT` 将对齐方式应用于当前正在处理的行。参数不是这三者之一时出错。

## 用法

### `ALIGNMENT <LEFT or CENTER or RIGHT>`
- `<LEFT or CENTER or RIGHT>`：字面对象参数，取 `LEFT`（左对齐）、`CENTER`（居中）、`RIGHT`（右对齐）。
```erb
ALIGNMENT RIGHT
PRINT 啊啊啊
ALIGNMENT CENTER
PRINTL 噢噢噢
ALIGNMENT LEFT
;上面的示例中「啊啊啊」「噢噢噢」将会居中对齐
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:285`（`argb[FunctionArgType.STR]`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:508`（`case FunctionCode.ALIGNMENT` 分支）

```text
case ALIGNMENT:
    str = func.Argument.ConstStr        # 字面常量参数（编译期已确定）
    若 str 忽略大小写等于 "LEFT":
        exm.Console.Alignment = DisplayLineAlignment.LEFT
    否则若等于 "CENTER":
        exm.Console.Alignment = DisplayLineAlignment.CENTER
    否则若等于 "RIGHT":
        exm.Console.Alignment = DisplayLineAlignment.RIGHT
    否则:
        抛出 CodeEE（InvalidAlignment，"无效的对齐方式：<str>"）
```

## 备注

- 源码把参数当作**字符串常量**（`ConstStr`，非运行时表达式），即 `ALIGNMENT` 后必须直接写 `LEFT/CENTER/RIGHT` 字面量；ecd 文档签名 `<LEFT or CENTER or RIGHT>` 与此一致。
- 对齐设置作用于控制台行对象（`Console.Alignment`），影响当前行及后续行，直到再次修改；`CURRENTALIGN` 指令可读回当前值。
- zh 套件无专节，仅 `HTML_PRINT.md` 提及 `HTML_PRINT` 绘图不受 `ALIGNMENT` 影响。
