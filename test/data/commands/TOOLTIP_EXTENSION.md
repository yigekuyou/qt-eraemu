# TOOLTIP_EXTENSION

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（**伪名，实际不存在此命令**）
- **签名**：
  - 无（该名字不是任何真实命令；它是文档页面/功能名）
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt:228`「・ツールチップ機能拡張 詳しくは→https://evilmask.gitlab.io/emuera.em.doc/Reference/TOOLTIP_EXTENSION/」；ecd `Command.md`「工具提示系」小节（2160 行前后）。**没有任何文档把它列为命令**。

## 语义

不存在名为 `TOOLTIP_EXTENSION` 的命令/函数。`TOOLTIP_EXTENSION` 是 EM 文档站（evilmask.gitlab.io/emuera.em.doc）中「工具提示功能扩展」的**参考页面名称**，EE readme 引用了该链接，命令清单提取时被误当成命令名。清单生成脚本（`test/export_command_tables.py` 的 `EE_EXTENSIONS` 手工清单，"工具提示扩展"分组）将其误收入。

### 伪名验证

按工作约定核查后确认它不是「前缀 F + 原名」模式（无 F 前缀），且：

1. `source_index.md:260` 标 **NOT FOUND**，`:481` 标 **ABSENT**；
2. EM+EE 发行版 exe（`eraTW/Emuera1824+v18+EMv17+EEv41.exe`）按 ASCII 与 UTF-16 检索均无 `TOOLTIP_EXTENSION` 标识符；
3. ERB_EXCOM.khp 关键字帮助无此名。

### 该页面实际记载的真实命令（EM/EE 工具提示扩展）

扩展工具提示需先用开关命令开启，之后下列命令才生效（均为命令、无返回值）：

- `TOOLTIP_CUSTOM <数值表达式>`：非 0 开启扩展工具提示（自绘模式），0 关闭并恢复传统显示。
- `TOOLTIP_SETCOLOR <前景色>, <背景色>`：以 `0xRRGGBB` 设置前景/背景色（ecd Command.md「工具提示系」，扩展开启后才生效）。
- `TOOLTIP_SETDELAY <毫秒>`：设置显示前等待，默认 500，最大 32767（ecd 同上）。
- `TOOLTIP_SETDURATION <毫秒>`：设置最大显示时间，0 为默认行为（ecd 同上）。
- `TOOLTIP_SETFONT <字体名>`、`TOOLTIP_SETFONTSIZE <字号>`、`TOOLTIP_FORMAT <TextFormatFlags 数值>`：字体、字号与排版标志（C# TextFormatFlags 枚举值）。
- `TOOLTIP_IMG <数值表达式>`（EE 追加）：见 `TOOLTIP_IMG.md`。

## 源码实现（emuera.em/Emuera）

本仓库（emuera.em/Emuera）未实现名为 `TOOLTIP_EXTENSION` 的命令——不存在。但上述扩展工具提示本体在本仓库有实现：`Runtime/Script/Statements/Instraction.Child.cs:2932`（`#region EE_TOOLTIP拡張`，`TOOLTIP_SETFONT/TOOLTIP_SETFONTSIZE/TOOLTIP_CUSTOM/TOOLTIP_FORMAT/TOOLTIP_IMG` 五个指令类），注册于 `Runtime/Script/Statements/FunctionIdentifier.cs:424-426`；绘制端在 `UI/Game/EmueraConsole.cs:1875-1987`（`ToolTip_Draw`/`ToolTip_Popup`/`CustomToolTip` 等）。

## 备注

- 结论：本文件作为「工具提示扩展功能组」的占位与索引，`TOOLTIP_EXTENSION` 应从 EE 扩展命令清单中剔除；组内各真实命令另有独立文档。
- ecd Command.md 只收录了 TOOLTIP_SETCOLOR/SETDELAY/SETDURATION 三个；TOOLTIP_CUSTOM/SETFONT/SETFONTSIZE/FORMAT 的语义出自 EM 文档站 Reference/TOOLTIP_EXTENSION 页。
