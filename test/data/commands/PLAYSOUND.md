# PLAYSOUND

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令
- **签名**：
  - `PLAYSOUND <文件名>`
  - `PLAYSOUND <文件名>, <重复次数>`（本仓库实现支持的第 2 参数）
- **文档来源**：EM+EE 在线文档「PLAYSOUND」；`EmueraEE_readme.txt`「・PLAYSOUND "ファイル名.拡張子"」条目；`ecd/Command.md` 未收录；zh 套件未收录。

## 语义

播放一次 `sound` 目录（`./sound/`）下指定的音频文件。v12 起支持最多 10 个通道，可同时播放最多 10 个文件：执行时从 10 个通道中挑选一个「空闲（未在播放或已播完）」的通道；若全部通道都在播放，则使用 0 号通道（抢占最旧的）。音量由 `SETSOUNDVOLUME`（0～100）统一调节，停止用 `STOPSOUND`。

文件名以字符串给出（支持 FORM 展开的表达式与字面量）。使用 WMP 库播放，WMP 能播的格式基本都能播。EE v1 加入；v12 扩展为 10 通道（changelog：`PLAYSOUND機能拡張 10チャンネルに対応`）。

## 用法

### `PLAYSOUND <文件名>`
- `<文件名>`：字符串表达式，`sound` 目录内的音频文件名（含扩展名）。
- 播放一次；文件不存在时静默不播放，无法播放时抛 CodeEE。
```erb
PLAYSOUND "se_click.wav"   ;播放 sound/se_click.wav 一次
```

### `PLAYSOUND <文件名>, <重复次数>`（本仓库实现）
- `<重复次数>`：数值表达式；实际重复次数取 `max(重复次数, 1)`，即最小为 1。
```erb
PLAYSOUND "se_alarm.wav", 3   ;重复播放 3 次
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:408`（`addFunction(FunctionCode.PLAYSOUND, new PLAYSOUND_Instruction())`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:361`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2692`（`PLAYSOUND_Instruction`）；声音目录 `Program.cs:341`（`#region EE_PLAYSOUND系`，`SoundDir = ExeDir/sound`）；通道数组 `Runtime/Script/Statements/Instraction.Child.cs:2695`（`public static Sound[] sound = new Sound[10]`）；播放后端 `Runtime/Utils/Sound.WMP.cs`

```text
class PLAYSOUND_Instruction : AInstruction
    构造: ArgBuilder = SP_HTML_PRINT（字符串表达式 + 可选整数）; flag = METHOD_SAFE | EXTENDED
    DoInstruction(exm, func, state):
        soundArg = (SpHtmlPrint)func.Argument
        datFilename = soundArg.IsConst ? soundArg.ConstStr
                                       : soundArg.Str.GetStrValue(exm)
        repeat = soundArg.Opt != null ? (int)max(soundArg.Opt.GetIntValue(exm), 1) : 1
        filepath = Path.GetFullPath(Program.SoundDir + datFilename)   # ./sound/<文件名>
        try:
            若 File.Exists(filepath):
                for i in 0..9:                    # 找空闲通道
                    若 sound[i] == null: sound[i] = new Sound()
                    若 !sound[i].isPlaying(): break
                若 i >= 10: i = 0                 # 全占用则抢占 0 号通道
                sound[i].play(filepath, repeat)
        catch:
            抛出 CodeEE（不兼容的音频文件）

# Sound.play（Sound.WMP.cs）: player.URL=filepath; loop=false; playCount=repeat; controls.play()
```

## 备注

- EE readme 与在线文档只记载「播放一次」，未提第 2 参数；本仓库实现（EM 系合并代码）支持可选的重复次数参数，且下限为 1。这属于「实现比文档多」的差异，已在用法中如实记录。
- 本仓库构建实际移除 WMP 后端、改用 NAudio（`Runtime/Utils/Sound.NAudio.cs`：wav/ogg/MediaFoundation，repeat>1 用 RepeatStream 实现）。对 ERB 语义一致，「依赖 WMP」的说明在本仓库构建下不成立。
- 通道分配策略（空闲优先、全占用抢占 0 号）来自源码，文档未记载。
