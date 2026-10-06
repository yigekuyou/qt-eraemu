# EXISTSOUND

- **类别**：EE 扩展命令（本仓库中实现为式中函数）
- **签名**：
  - `EXISTSOUND "<文件名.扩展名>"`（EE readme 所载命令形态，结果代入 `RESULT`）
  - `EXISTSOUND("<文件名.扩展名>")`（式中函数版，返回值为结果）
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・EXISTSOUND "ファイル名.拡張子"」「・EXISTSOUND("ファイル名.拡張子")」；`EmueraEE_changelog.txt`（EEv1 追加）。ecd 套件与 zh 套件均未收录本命令。

## 语义

判断 `sound` 文件夹内是否存在指定文件名的音频文件。存在则结果为 `1`，不存在则为 `0`。音频播放本身由 `PLAYSOUND`/`PLAYBGM` 完成，本命令只做存在性检查，常用于播放前防御。

本仓库没有实现命令形态（`RESULT` 代入版），只实现了式中函数形态；按 EE 发行版文档，两种形态语义相同，命令形态把结果写入 `RESULT`。

## 用法

### `EXISTSOUND("<文件名.扩展名>")`
参数为相对 `sound` 文件夹的文件名（含扩展名）。
```erb
IF EXISTSOUND("bgm01.mp3")
    PLAYBGM "bgm01.mp3"
ELSE
    PRINTL 音频文件不存在。
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:313`（`["EXISTSOUND"] = new ExistSoundMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7197`（`ExistSoundMethod`，`#region EE_EXISTSOUND`；`GetIntValue` 在 7205 行）

```text
构造: ReturnType = 整数; argumentTypeArray = [string]; CanRestructure = false

GetIntValue(exm, arguments):
    str      = arguments[0].GetStrValue(exm)
    filepath = Path.GetFullPath(".\sound\" + str)
    if File.Exists(filepath): return 1
    return 0
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准，两者一致。
- EE readme 中同时列出了命令形态与式中函数形态；本仓库源码中只有式中函数（Creator.cs 注册在函数表，而非命令表），命令形态在本仓库不存在。
- 路径按 `.NET` 的 `Path.GetFullPath` 解析，`str` 中含 `..\` 等相对路径成分时按普通文件系统规则解析（无额外限制）。
