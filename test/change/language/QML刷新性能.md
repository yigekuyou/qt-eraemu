# 移植差异 · QML 刷新性能与 eraTetris

## 定位结果

`eraTetris/ERB/MAIN.ERB` 的游戏循环每次绘制都会：

1. `DRAW_STAGE()` 输出整个棋盘；
2. 输出控制按钮；
3. `TONEINPUT` 等待输入；
4. `CLEARLINE LINECOUNT - FIRSTLINE` 删除本轮画面；
5. 收到输入后重新执行循环。

因此棋盘内容变化时，删除旧行并插入新行是脚本显示语义要求，不能简单改成只更新一两个 QML 属性，否则会留下旧方块或旧图层。

此前 `ConsoleBackend::notifyInputRequested()` 和 `notifyInputDone()` 还会对整个模型发出：

```cpp
emit dataChanged(index(0), index(rowCount() - 1));
```

输入状态实际只改变按钮的 `clickable` role，却会让所有文本、图片和 shape 行都重新取 `BlocksRole`，造成额外的整屏绑定重算。

## 修复

新增 `notifyButtonRowsChanged()`：

- 只扫描含 `ConsoleSegment::isButton` 的行；
- 只删除这些行的扁平化缓存；
- 只发该行的 `dataChanged`；
- `roles` 限定为 `ConsoleBackend::BlocksRole`；
- 普通文本、图片和 shape 行不会因为输入状态切换而重新生成。

Qt 官方 `QAbstractItemModel::dataChanged()` 文档规定，`topLeft/bottomRight` 可以限定变化范围，`roles` 可以限定实际变化的 role；空 role 列表才表示全部 role。Qt Quick 性能文档也要求减少绑定重算、保持 view delegate 简单，并使用适当的 C++ 模型增量通知。

## 性能边界

这项修复消除了输入状态切换带来的额外全量刷新。`CLEARLINE` 后重新绘制棋盘仍会产生真实的行删除/插入，这是 eraTetris 的当前脚本输出方式，属于必要重画。

继续优化时应使用 Qt Creator QML Profiler 和 `ConsoleBackend::perfReport()` 测量，重点区分：

- `dataChanged` 的行数和 role；
- `beginRemoveRows` / `beginInsertRows` 的行数；
- `lineFlattenBuilds` / `lineFlattenMs`；
- `ConsoleBlock` delegate 与 `glyphRuns` 的创建数量。

不要为了减少模型信号而发送不完整的棋盘数据，也不要在 QML 每帧用 JavaScript 重建整个舞台。
