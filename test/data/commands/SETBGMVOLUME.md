# SETBGMVOLUME

- **类别**：EE 扩展命令
- **签名**：
  - `SETBGMVOLUME <数值表达式>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「概要」节（・SETBGMVOLUME）；`EmueraEE_readme (English).txt`。ecd 文档（Command.md 等）未收录本命令；zh 套件未收录。

## 语义

修改 `PLAYBGM` 正在循环播放的 BGM 的音量。参数取 0～100。它是 EM+EE 声音功能（基于 Windows Media Player 库）的一部分，与 `SETSOUNDVOLUME`（针对 `PLAYSOUND` 的 10 个声道）相对，只作用于 BGM 通道。执行后立即对当前 BGM 播放器生效，没有返回值。

## 用法

### `SETBGMVOLUME <音量>`
- `<音量>`：数值表达式，文档约定取值 0～100。
```erb
PLAYBGM "bgm01.mp3"
SETBGMVOLUME 30
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:366`（enum `FunctionCode.SETBGMVOLUME`，`#region EE` 内）；`Runtime/Script/Statements/FunctionIdentifier.cs:413`（`addFunction(FunctionCode.SETBGMVOLUME, new SETBGMVOLUME_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2818`（`SETBGMVOLUME_Instruction`，`#region EE` 声音区）

```text
构造:
    ArgBuilder = 单个 INT_EXPRESSION 参数
    flag = METHOD_SAFE | EXTENDED

DoInstruction(exm, func, state):
    vol = (int)参数表达式.GetIntValue(exm)     # 只取一次参数
    bgm.setVolume(vol)                          # bgm 是 Instraction.Child.cs:2691 的
                                                # static Sound bgm = new Sound()
                                                # Sound 内部是 WindowsMediaPlayer
                                                # setVolume 即 player.settings.volume = vol
```

## 备注

- 文档说参数「0～100 を指定できます」，但源码不做任何范围检查/钳制，直接透传给 WMP 的 `settings.volume`；越界值的行为取决于 WMP。
- ecd 主文档完全没有收录声音系命令（PLAYSOUND/PLAYBGM/STOPBGM/STOPSOUND/SETSOUNDVOLUME/SETBGMVOLUME/EXISTSOUND），语义只能以 EE readme 为准。
- 音量修改对「正在播放及之后播放」的 BGM 都生效（播放器对象是全局单例）。
