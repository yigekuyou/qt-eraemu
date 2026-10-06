# 补写代理指南（清单遗漏命令）

复核阶段发现：权威导出清单 `test/data/emuera_standard_cmds.txt` 漏收了
`BuiltInFunctionCode.cs` 枚举中的 88 个成员。其中：
- 69 个是 PRINT / PRINTSINGLE 族后缀变体 → 由 `PRINT.md`、`PRINTSINGLE.md` 两个基名文档统一覆盖；
- 19 个是独立命令 → 各写一个 md。

这些命令**不在两套中文文档中**（ecd 与 Era-Chinese-Documentation 都未收录），属 EM/EE 私家版扩展或较新版本新增。
所以语义来源按以下优先级：
1. 源码实现（权威）：`Runtime/Script/Statements/Instraction.Child.cs` 的 `*_Instruction` 类、
   `Runtime/Script/Process.ScriptProc.cs`、注册处 `Statements/FunctionIdentifier.cs`；
2. `eraTW/README集/EmueraEE Readme/` 下的 EmueraEE_readme.txt / changelog / 私家改造版 readme（Shift-JIS 编码，可用 iconv 转码后读）；
3. ecd/zh 其它页面偶有提及（grep 全目录）。

## 输出模板（与既有文档保持一致）
```markdown
# <名字>

- **类别**：命令（Emuera 枚举成员；两套中文文档未收录，语义据源码与 EM/EE readme）
- **签名**：<从 ArgumentBuilder/参数构建器与源码校验推定的完整签名，含可选参数>
- **文档来源**：无中文文档收录；依据 <源码文件:行号 / readme 文件名>

## 语义
<作用、参数、副作用、可用上下文、错误行为>

## 用法
### <签名>
<参数说明 + erb 示例>

## 源码实现（emuera.em/Emuera）
- 注册：`FunctionIdentifier.cs:<行>`
- 实现：`<文件>:<行>`（类名）

```text
<伪代码>
```

## 备注
<是否 emuera.em 私有扩展、与文档的差异、与其他命令的关系>
```

## 硬性要求
1. 伪代码必须读真实源码后翻译，标注真实 `文件:行号`，行号要能用 grep 验证。
2. 签名要如实：从 `ArgumentBuilder.cs` 的参数构建器、`FunctionArgType.cs` 或指令类 `DoInstruction` 里的参数使用推得；不确定的标注「推定」。
3. 全文中文。
