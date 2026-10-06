# STOPSOUND

- **类别**：EE 扩展命令
- **签名**：
  - `STOPSOUND`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「概要」节（・STOPSOUND：PLAYSOUNDで再生中の音声ファイルを停止します）；`EmueraEE_readme (English).txt`。ecd 文档未收录；zh 套件未收录。

## 语义

停止 `PLAYSOUND` 正在播放的全部音效。无参数、无返回值。遍历 10 个音效声道，对每个仍在播放的声道执行停止；尚未创建的声道会先被惰性创建（之后即处于停止状态）。

## 用法

### `STOPSOUND`
- 无参数。
```erb
PLAYSOUND "se_a.wav"
PLAYSOUND "se_b.wav"
;……
STOPSOUND   ;全部音效一起停
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:362`（enum）；`Runtime/Script/Statements/FunctionIdentifier.cs:409`（`addFunction(FunctionCode.STOPSOUND, new STOPSOUND_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2737`（`STOPSOUND_Instruction`）

```text
构造:
    ArgBuilder = VOID（无参数）
    flag = METHOD_SAFE | EXTENDED

DoInstruction(exm, func, state):
    循环 i = 0 .. 9:                    # static Sound[] sound = new Sound[10]
        若 sound[i] == null: sound[i] = new Sound()
        若 sound[i].isPlaying():        # 按 WMP 的 playState 判断
            sound[i].stop()
```

## 备注

- ecd 主文档未收录声音系命令；语义以 EE readme 为准。
- 与 `STOPBGM` 相互独立：本命令不影响 BGM 通道，`STOPBGM` 也不影响音效通道。
- 停止以 WMP 播放状态（`wmppsPlaying`/`wmppsBuffering` 等为「播放中」）为准，paused 状态不会被 stop。
