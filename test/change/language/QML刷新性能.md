# 移植差异 · QML 历史模型与当前舞台分离

## Qt 官方依据

Qt 6.11 文档《Using C++ Models with Qt Quick Views》说明，复杂动态数据应使用 `QAbstractItemModel`/`QAbstractListModel`，模型通过 `dataChanged()`、`beginInsertRows()`、`beginRemoveRows()` 等标准信号通知 QML view。

Qt 6.11 `ListView` 文档说明：

- view 会按可见区域创建 delegate，并可用 `reuseItems` 复用；
- delegate 可能被销毁和重新创建，不应在 delegate 中保存状态；
- 模型变化通常按帧批处理，`forceLayout()` 才会立即处理挂起变化；
- `QSortFilterProxyModel` 可以过滤不应显示的模型项。

Qt 6.11 `Repeater` 文档说明，模型变化会触发 delegate 的添加和移除；因此只用过滤代理分离历史与舞台，仍可能让当前舞台随着源模型 `CLEARLINE` 的删行/插行重建。

## 当前实现

`ConsoleBackend` 现在提供两种 QML 模型：

- `historyModel`：历史日志代理模型，负责滚动、回看和日志行；
- `stageModel`：独立的 `ConsoleStageSnapshotModel`，负责当前舞台。

`stageModel` 只在 `ConsoleBackend::flush()` 的帧边界发布：

```text
脚本 CLEARLINE
  -> 修改历史源缓冲
脚本重新 PRINT 完整棋盘
  -> flush()
  -> 生成舞台快照
  -> 逐行比较旧快照
  -> 只对变化行发 dataChanged
```

这样 QML 不会看到 `CLEARLINE` 与下一次 `PRINT` 之间的空舞台。

当舞台行数相同时：

- 不发送 `rowsRemoved`；
- 不发送 `rowsInserted`；
- 只对内容变化的舞台行发送 `dataChanged`；
- 已有舞台 delegate 可以保留和复用。

只有舞台行数确实变化时，才发送行插入或删除信号。

每个舞台 block 仍保存源模型的 `lineIndex`，所以按钮点击、跨行图片和调试定位继续使用原始控制台行号。

## QML 结构

`Console.qml` 同时保留两个 `ListView`：

```text
历史 ListView：可滚动，使用 historyModel
当前舞台 ListView：固定在视口，使用 stageModel，interactive=false
```

舞台使用与历史相同的 `LineDelegate`，但只负责当前帧的固定行；历史视图仍保留原有 PageUp/PageDown/End 和滚动条语义。

`EraRender.qml` 开启：

```qml
stageMode: true
```

测试夹具默认仍关闭舞台模式，保持原有 QML 行模型测试的行号语义。

## 性能边界

这项改动消除了 Tetris 每轮 `CLEARLINE` 对当前舞台 delegate 的直接删行/插行影响。脚本仍会重画整个棋盘，C++ 仍需生成新舞台行数据，但 QML 当前舞台不再把每次历史源模型删行/插行当作自己的生命周期变化。

后续性能分析应使用：

- `ConsoleBackend::perfReport()` 的 `lineFlattenBuilds` / `lineFlattenMs`；
- Qt Creator QML Profiler；
- `stageModel` 的 `dataChanged`、`rowsInserted`、`rowsRemoved` 信号计数；
- QML delegate 创建和复用数量。
