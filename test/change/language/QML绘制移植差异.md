# Qt/C++ QML 绘制移植差异

> 本文记录 Qt/C++ 当前实现与 C# Emuera 绘制机制的差异。C# 参考语义见 `test/data/language/CSharp绘制机制.md`。

## 数据和绘制路径

C# 使用一个 `displayLineList` 保存历史显示行和当前画面，WinForms `PictureBox.OnPaint` 在每次需要绘制时计算可见范围，并直接在一个 Graphics 表面上绘制可见行。

Qt/C++ 使用 `ConsoleBackend` 保存控制台行，并把显示数据暴露给 QML：

```text
ERB PRINT / CLEARLINE
  -> ConsoleBackend 修改 ConsoleBuffer
  -> flush() 在帧边界发布模型变化
  -> historyModel 提供历史日志
  -> stageModel 提供当前舞台快照
  -> QML ListView 创建或复用可见 delegate
  -> LineDelegate 绘制文字、图片、按钮和形状
```

Qt 的 `ConsoleDisplayLine`、`ConsoleSegment` 等 C++ 数据结构对应 C# 的 `ConsoleDisplayLine` 和显示节点。QML 负责声明式布局和控件交互，C++ 模型负责行数据、按钮状态和标准模型通知。

## 为什么要拆分历史和当前舞台

C# 使用单个 WinForms 绘制表面，因此 `CLEARLINE` 删除和下一轮重画之间的中间状态不会直接造成控件 delegate 生命周期变化。

QML `ListView` 由模型信号驱动。若历史模型和当前舞台共用同一个可过滤模型，eraTetris 每轮 `CLEARLINE` 产生的 `rowsRemoved` / `rowsInserted` 会传播到当前舞台视图，可能导致舞台 delegate 删除、重建和绑定重新计算。这样会增加每帧场景图更新成本。

当前 Qt/C++ 实现将两者分开：

- `historyModel` 使用历史源模型和 `ConsoleLineProxyModel`，负责滚动、回看和日志行；
- `stageModel` 使用独立的 `ConsoleStageSnapshotModel`，负责当前舞台；
- `EraRender.qml` 开启 `stageMode: true`；
- `Console.qml` 使用两个 `ListView`，舞台视图 `interactive: false`，固定在当前视口。

## 舞台快照的更新时机

`ConsoleBackend::updateStageModels()` 在 `flush()` 的帧边界调用。eraTetris 的典型更新顺序为：

```text
CLEARLINE 修改历史缓冲
  -> 脚本重新输出完整棋盘
  -> flush()
  -> 取最后 gridRows 行生成舞台快照
  -> 与上一帧逐行比较
  -> 发出最小范围的 QAbstractItemModel 信号
```

因此 QML 不会观察到 `CLEARLINE` 后、下一轮 `PRINT` 前的空舞台。当前舞台只在一帧完成后发布。

当舞台行数不变时，模型不会发送 `rowsRemoved` 或 `rowsInserted`，而只对内容确实变化的行发送带角色范围的 `dataChanged`。已有 delegate 可以继续使用，QML 不需要按行重建舞台。

只有舞台行数变化时才发送插入或删除信号。每个舞台 block 保留源模型的 `lineIndex`，所以按钮点击、跨行图片和诊断定位仍然使用原始控制台行号。

## QML 模型通知约束

Qt 官方 `QAbstractItemModel` 文档要求模型通过标准信号通知数据变化：

- `dataChanged()` 表示已有项的数据变化；
- `beginInsertRows()` / `endInsertRows()` 表示插入；
- `beginRemoveRows()` / `endRemoveRows()` 表示删除；
- `modelReset()` 表示整体模型重建。

`dataChanged()` 的 `roles` 参数指定变化的角色时，视图只需重新取这些角色。当前输入按钮状态的变化只扫描含按钮的行，并只发 `BlocksRole`，不会让普通文本、图片或 shape 行重新取全部角色。

这与 C# `RefreshStrings` 的节流目的相同，都是减少没有实际显示变化时的工作；实现位置不同：C# 在刷新请求层做判断，Qt 在模型通知范围和 QML delegate 复用层做判断。

## 当前实现与 C# 的对应关系

| C# 参考实现 | Qt/C++ 移植实现 |
| --- | --- |
| `displayLineList` | `ConsoleBuffer` 和历史模型 |
| `ConsoleDisplayLine.DrawTo` | QML `LineDelegate` 以及 C++ flatten 后的 block 数据 |
| `RefreshStrings(false)` 帧间隔节流 | `flush()` 帧边界发布、脏状态合并和模型增量通知 |
| `OnPaint(Graphics)` 只绘制可见行 | QML `ListView` 只创建可见 delegate，并复用 delegate |
| WinForms 双缓冲 PictureBox | Qt Quick scene graph 和 QML view 合成 |
| `lastDrawnLineNo` / 按钮状态判断 | 模型变化比较、角色限定的 `dataChanged` |
| `bitmapCacheEnabled` / bitmap cache | 当前以 C++ 行数据和 QML 图片资源为基础；尚未等价实现 C# 的位图表面缓存 |
| 单一 `displayLineList` 同时服务历史和当前画面 | 历史 `historyModel` 与当前舞台 `stageModel` 分离 |

## 性能边界和后续测量

历史与舞台分离解决了当前舞台随 `CLEARLINE` 行删除/插入而发生 delegate 生命周期变化的问题。它仍然需要在每个完成帧生成舞台行的 `QVariant` 数据，并对变化行更新 QML 内容；因此它没有完全复制 C# 的 bitmap/texture cache 策略。

验证性能时应记录：

- `ConsoleBackend::perfReport()` 的 `lineFlattenBuilds` 和 `lineFlattenMs`；
- `stageModel` 的 `dataChanged`、`rowsInserted`、`rowsRemoved` 次数；
- QML delegate 创建、复用和销毁次数；
- Qt Creator QML Profiler 中的绑定重算和帧时间；
- eraTetris 实际运行时的帧率和输入响应。

若舞台数据生成成为瓶颈，后续可评估 C++ 绘制表面或图片/纹理缓存。但当前模型分离已经保证历史日志更新不会直接重建固定舞台 delegate。
