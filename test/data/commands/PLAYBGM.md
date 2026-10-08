# PLAYBGM

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令
- **签名**：
  - `PLAYBGM <文件名>`
- **文档来源**：EM+EE 在线文档「PLAYBGM」；`EmueraEE_readme.txt`「・PLAYBGM "ファイル名.拡張子"」条目；`ecd/Command.md` 未收录；zh 套件未收录。

## 语义

循环播放 `sound` 目录（相对 Emuera 运行目录的 `./sound/`）下指定的音频文件。文件名以字符串（可含 FORM 展开的表达式）给出。使用 Windows Media Player 的库（WMPLib）播放，凡是 WMP 能播放的格式基本都能播放；最多可同时播放 10 个音频文件（PLAYSOUND 系与 PLAYBGM 各自管理通道，BGM 为单一专用通道）。

EE v1 加入（changelog：`関数追加：PLAYSOUND,STOPSOUND,PLAYBGM,STOPBGM,...`）。配套命令：`STOPBGM`（停止）、`SETBGMVOLUME`（音量 0～100）。注意：EE readme 提到若出现 `FunctionIdentifier` 类型初始化异常，可能是未安装 Windows Media Player 所致。

## 用法

### `PLAYBGM <文件名>`
- `<文件名>`：字符串表达式，`sound` 目录内的音频文件名（含扩展名）。
- 文件不存在时静默不播放（不报错）；文件存在但无法播放时抛出 CodeEE（音频文件不兼容）。
```erb
PLAYBGM "title.mp3"      ;循环播放 sound/title.mp3
;……
STOPBGM                  ;停止 BGM
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:410`（`addFunction(FunctionCode.PLAYBGM, new PLAYBGM_Instruction())`；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:363`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2756`（`PLAYBGM_Instruction`）；声音目录定义 `Program.cs:341`（`SoundDir = ExeDir/sound`）；播放后端 `Runtime/Utils/Sound.WMP.cs`（WMPLib）

```text
class PLAYBGM_Instruction : AInstruction
    构造: ArgBuilder = STR_EXPRESSION（1 个字符串表达式）; flag = METHOD_SAFE | EXTENDED
    DoInstruction(exm, func, state):
        arg = (ExpressionArgument)func.Argument
        datFilename = arg.IsConst ? arg.ConstStr : arg.Term.GetStrValue(exm)
        filepath = Path.GetFullPath(Program.SoundDir + datFilename)   # ./sound/<文件名>
        try:
            若 File.Exists(filepath):
                bgm.play(filepath, -1)        # -1 表示无限循环
        catch:
            抛出 CodeEE（不兼容的音频文件）
    # 静态字段：Instraction.Child.cs:2696 附近
    #   public static Sound bgm = new Sound();    # BGM 专用单一通道

# Sound.play(filepath, repeat=-1)（Sound.WMP.cs，WMPLib 实现）:
    player.URL = filepath
    若 repeat == -1: settings.playCount = 1; settings.setMode("loop", true)   # 无限循环
    否则:            settings.setMode("loop", false); settings.playCount = repeat
    player.controls.play()
```

## 备注

- 文档与源码语义一致。两点实现细节：文件不存在时只是不播放（无错误）；循环通过 WMP 的 loop 模式实现。
- 本仓库构建（csproj `Compile Remove="Runtime/Utils/Sound.WMP.cs"`）实际改用 NAudio 后端（`Runtime/Utils/Sound.NAudio.cs`）：`play(filepath, -1)` 用 `LoopStream` 实现无限循环，支持 wav/ogg 及 MediaFoundation 可解的其他格式。对 ERB 而言语义不变，但「依赖 WMP」的文档说明在本仓库构建下不成立。
- 与 PLAYSOUND 的 10 通道不同，BGM 只有 1 个通道，后播的 PLAYBGM 会替换当前 BGM。
