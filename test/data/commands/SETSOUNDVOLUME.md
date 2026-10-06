# SETSOUNDVOLUME

- **类别**：EE 扩展命令
- **签名**：
  - `SETSOUNDVOLUME <数值表达式>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「概要」节（・SETSOUNDVOLUME）；`EmueraEE_readme (English).txt`。ecd 文档未收录；zh 套件未收录。

## 语义

修改 `PLAYSOUND` 播放音效的音量。参数取 0～100。与 `SETBGMVOLUME` 不同，它一次性设置全部 10 个音效声道（EE v12 起 `PLAYSOUND` 支持最多 10 个文件同时播放）的音量，且对尚未创建的声道也会先创建 Sound 对象再设置。没有返回值。

## 用法

### `SETSOUNDVOLUME <音量>`
- `<音量>`：数值表达式，文档约定取值 0～100。
```erb
PLAYSOUND "se_click.wav"
SETSOUNDVOLUME 50
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:365`（enum）；`Runtime/Script/Statements/FunctionIdentifier.cs:412`（`new SETSOUNDVOLUME_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2799`（`SETSOUNDVOLUME_Instruction`）

```text
构造:
    ArgBuilder = 单个 INT_EXPRESSION 参数
    flag = METHOD_SAFE | EXTENDED

DoInstruction(exm, func, state):
    vol = (int)参数表达式.GetIntValue(exm)
    循环 i = 0 .. 9:                     # Instraction.Child.cs:2690
                                         # static Sound[] sound = new Sound[10]
        若 sound[i] == null: sound[i] = new Sound()   # 惰性创建声道
        sound[i].setVolume(vol)           # 即 WMP player.settings.volume = vol
```

## 备注

- 与 `SETBGMVOLUME` 同样，源码不检查 0～100 范围，直接透传给 WMP。
- 设置的是全部 10 个声道的音量，无法按声道单独设置。
- ecd 主文档未收录声音系命令；语义以 EE readme 为准。
