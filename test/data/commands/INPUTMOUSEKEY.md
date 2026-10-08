# INPUTMOUSEKEY

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令
- **签名**：
  - `INPUTMOUSEKEY`
  - `INPUTMOUSEKEY <限制时间>`
- **文档来源**：**两套文档均未收录本命令的命令小节**。ecd 套件仅在 `Command.md`「图像处理相关」CBG 按钮映射一节提到「这里设置的按钮映射会影响 `CBGSETBUTTONSPRITE` 指令和 `INPUTMOUSEKEY` 指令」；zh 套件完全未提及。以下语义主要依据源码归纳。

## 语义

等待用户的一次原始鼠标/按键输入（`PrimitiveMouseKey` 类型输入请求），把输入结果交给控制层处理（配合 `INPUTMOUSEKEY` 专用变量族如 `MOUSE_X`、`MOUSE_Y` 及按键/滚轮状态变量使用；与 CBG 按钮映射联动时可识别点击的按钮）。

可以传一个正整数作为限制时间（毫秒），超时后不等待输入直接继续；不传或传非正值则无限等待。该指令不可被消息快进跳过（构造函数注释「スキップ不可」，相关 flag 被注释掉，仅保留 `EXTENDED`）。

## 用法

### `INPUTMOUSEKEY` / `INPUTMOUSEKEY <限制时间>`
- `<限制时间>`：整数表达式（毫秒）。大于 0 时设置为本次输入请求的时限；0 或负值、省略时无时限。可省略。
```erb
PRINTL 请点击画面上的按钮（5 秒内）
INPUTMOUSEKEY 5000
;之后用 MOUSE_X / MOUSE_Y / 按键变量等检查输入结果
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:396`（`new INPUTMOUSEKEY_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2182`（`INPUTMOUSEKEY_Instruction`，flag = `EXTENDED`，参数 builder 为 `GetNormalArgumentBuilder("I", 0)`——整数、可全部省略）

```text
DoInstruction(exm, func, state):
    arg = (ExpressionsArgument)func.Argument
    time = 0
    若 arg.ArgumentArray.Count > 0:
        time = arg.ArgumentArray[0].GetIntValue(exm)
    req = new InputRequest { InputType = PrimitiveMouseKey }
    若 time > 0:
        req.Timelimit = (int)time
    exm.Console.WaitInput(req)    # 阻塞等待一次鼠标/键盘原始输入，超时则直接放行
```

## 备注

- **文档未收录**：ecd/zh 两套文档均无本命令的签名与语义小节，以上语义完全由源码（`INPUTMOUSEKEY_Instruction`）推导；「与 CBG 按钮映射联动」「配合 `MOUSE_X` 等变量使用」依据 ecd `Command.md:2606` 的旁述与同族指令的一般用法。
- 源码中 `flag = IS_PRINT | IS_INPUT | EXTENDED` 被注释掉、只保留 `EXTENDED`（注释：スキップ不可），即本指令不参与打印缓冲刷新与普通输入标记流程，是与其他 INPUT 系指令的一个实现差异。
- 本仓库为 Emuera1824 系（EE）实现；更早的私修版实现细节（返回值写入哪些变量）由控制层（`WaitInput` 的 `PrimitiveMouseKey` 处理）决定，不在本指令类内。
