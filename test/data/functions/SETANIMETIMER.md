# SETANIMETIMER

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（另有同名命令形态）
- **签名**：int SETANIMETIMER(int time)
- **文档来源**：`ecd/Command.md`「### SETANIMETIMER `<时间>`」（命令形态）；`ecd/Expression.md` 未收录签名；`ecd/ERB_Commands.md` 归入「图像处理相关」

## 语义

为动画精灵指定重绘间隔（毫秒）。Emuera 通常不会在 `INPUT` 等输入等待期间重绘画面；通过本函数设置重绘间隔后，输入等待期间动画精灵也可以动起来。

注意：
- 在 `TINPUT` 等带超时处理的指令中不会重绘。
- 实际重绘间隔受计算机状态影响，会比指定值略晚；若把间隔设成与动画帧 delay 相同的值会频繁掉帧，应指定比 delay 足够小的间隔。
- 与配置中的「每秒帧数」项目无关，也不受 `REDRAW` 指令抑制重绘效果的影响（ecd/Resource.md 还提到：设置后可在 INPUT 期间重绘）。

函数形态设置成功后返回 `1`。参数超出范围时抛出 CodeEE（见实现：实际上限是 `short.MaxValue`，但错误信息写的是 `int.MaxValue`，两处不一致）。

同名命令形态 `SETANIMETIMER <时间>`（本仓库另通过 SP_GETINT 通道注册为命令，Process.ScriptProc.cs 有对应 switch-case），本文档以式中函数形态为主。

## 用法

### int SETANIMETIMER(int time)
- `time`：重绘间隔，毫秒。实际取值范围为 `int.MinValue ～ 32767`（short.MaxValue），超出抛 CodeEE。
- 返回值：设置成功返回 `1`。
```erb
	SPRITEANIMECREATE "アニメ", 100, 100
	; ……SPRITEANIMEADDFRAME 添加帧……
	SETANIMETIMER(33)	; 约 30fps 重绘，让 INPUT 期间动画也能播放
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:211`（`["SETANIMETIMER"] = new SetAnimeTimerMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6876`（`SetAnimeTimerMethod`）

```text
构造：返回类型 = long；参数 = [long]；CanRestructure = false（有全局副作用，禁止折叠）。

GetIntValue(exm, args):
    i64 ← args[0].GetIntValue(exm)
    若 i64 < int.MinValue 或 i64 > short.MaxValue:
        抛出 CodeEE（"第 1 参数超出范围"，错误信息声称范围为 int.MinValue～int.MaxValue）
    exm.Console.setRedrawTimer((int)i64)   ; 设置控制台的重绘定时器（全局副作用）
    返回 1
```

## 备注

- ecd/Command.md 只有命令形态的小节（无返回值叙述）；ecd/Expression.md 连函数签名都未收录，签名取自源码 `argumentTypeArray = [typeof(long)]`。
- 源码不一致处：范围检查上限是 `short.MaxValue`（32767），但错误信息里声称上限是 `int.MaxValue`——以实际检查为准。
- 命令文档说「以毫秒为单位指定重绘间隔」，与源码 `setRedrawTimer((int)i64)` 一致。
- 相关：动画精灵的创建与加帧见 `SPRITEANIMECREATE` / `SPRITEANIMEADDFRAME`；资源文件一节（ecd/Resource.md）亦提到 SETANIMETIMER。
- zh 套件仅在 Resource.md 提及该指令，无独立小节。
