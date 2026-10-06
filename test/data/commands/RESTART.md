# RESTART

- **类别**：命令
- **签名**：RESTART
- **文档来源**：`ecd/docs/translation/Command.md` 未单独收录；见 `ecd/docs/translation/ERB_File_Format.md`「函数」一节及 `ecd/Custom_Variable.md:222`；Era-Chinese-Documentation 未收录该命令

## 语义

使当前函数从头重新开始执行（跳转回本函数 `@` 标签的第一行）。无参数、无返回值。注意：RESTART 是「跳回函数开头」而不是「重新初始化」，因此动态分配的变量（`DITEMTYPE`、`DA` 等动态变量）不会被重置。由于跳回开头，若函数体中没有退出路径，会形成无限循环。

## 用法

### RESTART
无参数。

```erb
@ENDING
PRINTW 游戏结束。
RESTART    ;跳回 @ENDING 开头，永远重复显示
```

```erb
@INPUT_LOOP
INPUT
IF RESULT == 0
	RESTART    ;输入无效则从头重新执行本函数
SIF RESULT == 999
	RETURN 0
;...正常处理
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:355` → `new RESTART_Instruction()`（注释：関数の再開。関数の最初に戻る）；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:105`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3427`（`RESTART_Instruction`）

```text
类 RESTART_Instruction:
  构造: 参数构造器 = VOID; 标志 = METHOD_SAFE | FLOW_CONTROL | EXTENDED

  DoInstruction(exm, func, state):
    state.JumpTo(func.ParentLabelLine)   // 直接跳转到当前指令所属函数标签的开头
                                         // 即从本函数第一行重新顺序执行
```

## 备注

- `JumpTo(func.ParentLabelLine)` 表明 RESTART 的目标就是「包含该指令的函数标签」，与文档「当前函数从头重新开始执行」一致。
- ecd 的 Command.md 没有为 RESTART 单独设小节，语义散见于 ERB_File_Format.md（示例中 @ENDING 的 RESTART 造成无限重复）与 Custom_Variable.md（说明动态变量不会被 RESTART 重置）。
- Era-Chinese-Documentation 未收录本命令。
