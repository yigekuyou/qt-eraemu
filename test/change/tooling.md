# 移植差异 · 测试工具与覆盖记录

- [冒烟清单校正指南](tooling/GUIDE.md)
- [输入注入指南](tooling/INPUT_GUIDE.md)
- [历史覆盖快照](coverage_report.txt)（静态用例收录，不等于执行通过）
- [C# 语义手册](../data/README.md)

清单与夹具保持 `test/data/doc_smoke.tsv`、`test/data/_smoke_fix/` 路径；工具指南中的输入输出均相对仓库根。
C# 的缺失实现与 C++ 的当前实现状态必须分别判断；历史覆盖数字不是本轮测试结果。
CALLSHARP 的跳过背景见 [移植取舍](commands.md#callsharp)。

## 未确证名字的测试推测

从 [OCLEARLINE](../data/commands/OCLEARLINE.md) 迁入：旧文按
`test/example/ERB/16_DISPLAY.ERB:31-33` 和 `doc_smoke.tsv` 推测签名为
`OCLEARLINE <行数>`，示例 `OCLEARLINE 1`，把效果猜作“删除已显示行”，
进一步猜测作用于 EM 分栏/覆盖显示。即使测试仅验证执行不报错，也不能推出这些原版语义。
这些都是待证的本地测试假设，不能作为兼容标准。

从 [LCSVISASSI](../data/commands/LCSVISASSI.md) 迁入：原记录称组 35
`test/example/ERB/35_DOC_SMOKE.ERB` 只以裸名字造桩，未提供参数与语义。
名字出现在覆盖清单不等于 C# 实现或真实游戏用例存在。
