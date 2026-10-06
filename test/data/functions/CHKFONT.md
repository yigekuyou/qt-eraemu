# CHKFONT

- **类别**：式中函数
- **签名**：`int CHKFONT(str fontname)`
- **文档来源**：`ecd/Expression.md`（函数目录签名）、`ecd/Command.md`「CHKFONT `<字符串表达式>`」小节；zh 套件未收录

## 语义

检查系统是否已安装指定名称的字体。已安装返回 `1`，未安装返回 `0`。

考虑指定的字体有可能未被安装，建议在使用 `SETFONT` 指令前先用 `CHKFONT` 检查（`SETFONT` 指定不存在的字体时会静默替换为 `Microsoft Sans Serif`）。

作为式中函数，返回值直接在表达式中取得（ecd 文档表述为「返回数值 1 到 RESULT:0 中」，即 `A = CHKFONT("...")` 或 `RESULT:0 = CHKFONT(...)` 的惯用法）。

## 用法

### int CHKFONT(str fontname)
- `fontname`：字体名称（字符串表达式）。
```erb
CHKFONT "ＭＳ Ｐゴシック"
IF RESULT
    SETFONT "ＭＳ Ｐゴシック"
    PRINTL abc123(ＭＳ Ｐゴシック)
ENDIF
STR:0 = ＭＳ 明朝
IF CHKFONT(STR:0)
    SETFONT STR:0
    PRINTL abc123(ＭＳ 明朝)
ENDIF
SETFONT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:38`（`["CHKFONT"] = new CheckfontMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2399`（`CheckfontMethod`，返回 long，参数 `[string]`，`CanRestructure = true`）

```text
函数 CHKFONT(str):
    isInstalled ← 0
    遍历系统已安装字体集合（InstalledFontCollection.Families）:
        若 ff.Name == str: isInstalled ← 1; 跳出
    遍历程序私有字体集合（GlobalStatic.Pfc.Families，EE 加入的字体文件支持）:
        若 ff.Name == str: isInstalled ← 1; 跳出
    返回 isInstalled
```

## 备注

- ecd 文档为旧版描述，未提及 EE 的「字体文件支持」（EE_フォントファイル対応）：除系统安装字体外，还会检查 Emuera 私有字体集合（`GlobalStatic.Pfc`，程序启动时/运行中通过字体文件加载的字体）。
- `CanRestructure = true`（源码注释：起動中に変わることもそうそうないはず……），即编译期可能把结果作为定值缓存；若运行中动态加载字体，理论上可能取到旧值，但一般无影响。
- 名称比较是精确字符串比较（区分大小写等，按 .NET 字符串 `==`），字体名需与系统内名称完全一致。
