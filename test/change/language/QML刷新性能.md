# QML 刷新性能移植记录

C# Emuera 的绘制机制和刷新节流记录在 [`test/data/language/CSharp绘制机制.md`](../../data/language/CSharp绘制机制.md)。其中包括 `RefreshStrings`、`OnPaint`、可见行绘制、WinForms 双缓冲、`ConsoleDisplayLine` 和 bitmap cache。

Qt/C++ 的当前实现和移植差异记录在 [`QML绘制移植差异.md`](QML绘制移植差异.md)。

当前实现的关键点是：

- 历史日志使用 `historyModel`；
- 当前舞台使用独立的 `ConsoleStageSnapshotModel`；
- 舞台快照在 `ConsoleBackend::flush()` 帧边界发布；
- 舞台行数不变时只对变化行发送 `dataChanged`；
- 只有舞台行数变化时才发送 `rowsInserted` / `rowsRemoved`；
- 输入按钮状态变化只通知含按钮的行，并限定为 `BlocksRole`；
- 舞台 block 保存源模型的 `lineIndex`，保证按钮、跨行图片和诊断定位继续使用原始行号。

这项移植减少了 eraTetris 每轮 `CLEARLINE` 对当前舞台 delegate 生命周期的影响。后续性能验证应使用 `ConsoleBackend::perfReport()`、Qt Creator QML Profiler、模型信号计数和实际 eraTetris 帧率；C# 的 bitmap/texture cache 尚未在 QML 中完全等价实现。
