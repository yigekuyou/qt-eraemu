# STOPBGM

- **类别**：EE 扩展命令
- **签名**：
  - `STOPBGM`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「概要」节（・STOPBGM：PLAYBGMで再生中の音声ファイルを停止します）；`EmueraEE_readme (English).txt`。ecd 文档未收录；zh 套件未收录。

## 语义

停止 `PLAYBGM` 正在循环播放的 BGM。无参数、无返回值。与 `STOPSOUND`（停止 `PLAYSOUND` 的全部音效声道）相对，只作用于 BGM 通道。

## 用法

### `STOPBGM`
- 无参数。
```erb
PLAYBGM "bgm01.mp3"
;……
STOPBGM
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:364`（enum）；`Runtime/Script/Statements/FunctionIdentifier.cs:411`（`addFunction(FunctionCode.STOPBGM, new STOPBGM_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2786`（`STOPBGM_Instruction`）

```text
构造:
    ArgBuilder = VOID（无参数）
    flag = METHOD_SAFE | EXTENDED

DoInstruction(exm, func, state):
    bgm.stop()      # bgm 是全局唯一 Sound 实例（Instraction.Child.cs:2691）
                    # Sound 内部是 WindowsMediaPlayer，
                    # stop() 即 player.controls.stop()
```

## 备注

- ecd 主文档未收录声音系命令；语义以 EE readme 为准。
- 只停止 BGM 通道；`PLAYSOUND` 播放的音效不受影响（那要用 `STOPSOUND`）。
- 播放文件本身放在 `sound` 目录下（`PLAYBGM` 时以 `Program.SoundDir` 为基准解析相对路径）。
