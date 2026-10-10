# C# Emuera 绘制机制

> 本文记录 C# Emuera 参考实现的绘制语义，来源是仓库内的 `emuera.em/Emuera/` 源码。它描述 C# 的实际实现，供 Qt/C++ 移植时对照。

## 绘制对象和数据流

C# 控制台把已经排版的输出保存为 `List<ConsoleDisplayLine> displayLineList`。一条 `ConsoleDisplayLine` 包含多个 `ConsoleButtonString`，每个按钮字符串又包含文字、图像、形状等显示节点。输出过程先由 `PrintStringBuffer` 收集文字和样式，随后转换为 `ConsoleDisplayLine` 并加入 `displayLineList`。

典型流程如下：

```text
ERB PRINT / PRINTFORM / HTML_PRINT
  -> PrintStringBuffer 收集文本、样式、按钮和图片
  -> Flush / FlushSingleLine
  -> ConsoleDisplayLine
  -> EmueraConsole.addDisplayLine
  -> displayLineList
  -> RefreshStrings
  -> WinForms PictureBox.Refresh
  -> MainPicBox.OnPaint(Graphics)
  -> 绘制可见 ConsoleDisplayLine
```

`addDisplayLine` 会为显示行设置对齐方式和逻辑行号，并处理以下状态：

- 删除上一条临时行；
- 检查非法字体；
- 注册跨行绘制的 `EscapedParts`；
- 当上一行 `IsLineEnd == false` 时，把新行接到上一行后面；
- 超过 `Config.MaxLog` 时从头部删除旧行；
- 更新 `lineNo` 和 `logicalLineCount`；
- 记录 `bitmapCacheEnabledForNextLine` 到新行的 `bitmapCacheEnabled`。

## RefreshStrings：何时请求重绘

`EmueraConsole.RefreshStrings(bool force_Paint)` 不直接绘制内容，而是决定是否请求 WinForms 产生一次 `OnPaint`。

当同时满足以下条件时，普通刷新会被跳过：

- `redraw == ConsoleRedraw.None`；
- `force_Paint == false`；
- 当前没有回看历史日志。

非强制刷新还会跳过以下情况：

- 当前不在历史日志中；
- 最后一条显示行已经绘制；
- 当前选中的按钮没有变化。

脚本运行或初始化期间，如果距离上次帧刷新还没有达到 `msPerFrame`，刷新也会延后。输入等待等长时间没有脚本输出的状态会触发强制刷新，避免界面一直不更新。

最终通过：

```text
window.Refresh()
```

向 WinForms 控件请求 `OnPaint`。因此 C# 的刷新是“输出数据先进入显示行列表，刷新请求再触发绘制”，而不是每次打印都立即重新绘制全部控件。

## OnPaint：全面清屏后绘制可见内容

`EmueraConsole.OnPaint(Graphics graph)` 是主绘制入口。它首先根据滚动条计算可见范围：

- `bottomLineNo`：当前视口底部的显示行号；
- `topLineNo`：根据窗口高度和 `Config.LineHeight` 算出的顶部显示行号；
- `pointY`：顶部显示行的纵向位置。

普通 Graphics 绘制模式下，绘制过程为：

1. 校验或生成背景缓存；
2. 用背景色清空绘图区；
3. 绘制已经烘焙的背景图；
4. 按深度绘制 CBG 和跨行扩展部件；
5. 从 `topLineNo` 到 `bottomLineNo` 调用 `ConsoleDisplayLine.DrawTo`；
6. 绘制 HTML island、Rikaichan 等附加层。

显示行的绘制调用大致是：

```csharp
for (int i = topLineNo;
     i <= bottomLineNo && i < displayLineList.Count;
     i++)
{
    displayLineList[i].DrawTo(
        graph, pointY, isBackLog, true, Config.TextDrawingMode);
    pointY += Config.LineHeight;
}
```

C# 使用一个主 PictureBox 的绘图区和一次 OnPaint 完成当前画面的合成。绘制内容按可见行筛选，历史日志中的不可见行不会逐行调用 `DrawTo`。

源码注释说明，全面清除方式下由 WinForms 的双缓冲机制负责减少闪烁。C# 的模型仍然保留在 `displayLineList` 中，绘制表面则在 `OnPaint` 中重建当前可见画面。

## Tetris 等动画场景

eraTetris 的脚本每轮执行类似以下步骤：

```text
DRAW_STAGE
  -> 输出当前棋盘行和按钮
  -> TONEINPUT 等输入等待
  -> CLEARLINE 清除当前棋盘行
  -> 下一轮重新 DRAW_STAGE
```

C# 中 `CLEARLINE` 修改 `displayLineList`，随后通过 `RefreshStrings` 请求刷新。下一次 `OnPaint` 会重新计算可见行并绘制当前棋盘。

它能保持可用性能的主要原因是：

- `RefreshStrings(false)` 会跳过过于频繁的刷新；
- 已经完成的显示内容保存在 `ConsoleDisplayLine`，脚本执行和绘制阶段分开；
- `OnPaint` 只遍历当前视口内的行；
- WinForms PictureBox 使用双缓冲合成画面；
- 按钮、复杂显示字符串和图像可以使用绘制节点或 bitmap cache；
- 没有为每个显示行创建 QML delegate，也没有让每个历史行参与 Qt Quick 的绑定重算。

`RefreshStrings` 的节流并不改变显示行数据，只是推迟画面提交。输入状态、按钮选择或强制重绘发生时，可以绕过普通帧间隔限制。

## 文本、图片和形状的绘制

`ConsoleDisplayLine.DrawTo` 依次调用其中的 `ConsoleButtonString.DrawTo`。文字节点通常使用以下两条路径之一：

- `Graphics.DrawString`；
- `TextRenderer.DrawText`。

图片节点通过 `ASprite.GraphicsDraw` 绘制，形状和跨行节点由各自的 `DrawTo` 方法绘制。按钮状态会影响文字颜色、背景色或按钮图片的选择。

`BITMAP_CACHE_ENABLE` 只影响后续显示行的缓存标记。C# 源码把全局的 `bitmapCacheEnabledForNextLine` 写入新建的 `ConsoleDisplayLine.bitmapCacheEnabled`；该标记不会在每一行后自动清除，直到再次调用相关函数改变它。缓存适用于复杂按钮字符串等重复绘制成本较高的内容。

## 与数据模型的关系

C# 的历史日志和当前画面共享 `displayLineList`。滚动条改变时，`OnPaint` 根据新的可见范围从同一个列表重新绘制；回看历史不会复制一个独立的舞台模型。

因此 C# 的主要优化单位是：

- 刷新请求是否被节流；
- 当前 OnPaint 是否只绘制可见行；
- 显示节点和图片是否可以复用或缓存；
- WinForms 双缓冲是否减少了呈现开销。

Qt/C++ 移植采用 QML 视图模型，需要额外处理模型信号、delegate 生命周期和历史日志与当前舞台之间的更新隔离。这些内容记录在 `test/change/language/QML绘制移植差异.md`。
