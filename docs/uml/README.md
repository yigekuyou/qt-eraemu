# emuera UML

这些 PlantUML 源文件描述当前实现的模块边界和跨边界协作。每张图只承担一个责任，源码路径写在图头部；渲染出的 PNG/SVG 属于本地产物，不纳入版本控制。

## 图索引

| 文件 | 图的责任 |
| --- | --- |
| `00-overview.puml` | 模块边界与依赖方向 |
| `01-load-classes.puml` | Content、GameData/ast、GameProc 的装载类型边界 |
| `02-runtime-step.puml` | `ScriptRunner` 步进、调用栈、`ExecState` |
| `03-runtime-system.puml` | `SystemStateMachine`、`SystemHost`、系统状态与输入等待 |
| `04-runtime-console-qml.puml` | `ExecutionEngine` 输出、`ConsoleBackend` 输入桥、QML 呈现与帧节拍 |
| `05-runtime-save-load.puml` | `SAVEGAME`/`LOADGAME`、`SystemHost` 回调和变量快照集成 |
| `06-expression-ast.puml` | `LogicalLine`、表达式节点和装载期访问描述符 |
| `07-variable-resolution.puml` | 作用域、名字到 locus 的唯一解析顺序 |
| `08-variable-storage.puml` | 存储容器、数组几何、`erasave::Map` 条目映射 |
| `09-diagnostics.puml` | 装载诊断数据结构、pass 切片和所有权 |
| `10-static-checks.puml` | 静态目标、结构和常量检查目录 |
| `11-save-format.puml` | `EraBinaryData` 编码、版本和失败原子性 |
| `12-load-pipeline.puml` | Content 输入到 AST、load pass 和诊断的时序 |

运行时故意拆成四张图：步进只描述脚本执行；系统图只描述帧底交接、状态分派和等待恢复；控制台图只描述信号、QML 属性和提交路径；存档图只描述运行时回调与快照。这样一个类只在拥有该责任的图中展开，跨图只保留接口关系。

## 源码边界

- `src/eraengine/Content` 负责 ERB/CSV 输入、编码和预处理。
- `src/eraengine/GameData/ast` 负责 AST、声明事实和装载诊断。
- `src/eraengine/GameData/Variable` 负责运行时容器、形状和存档条目映射。
- `src/eraengine/GameProc` 负责解析表、装载 pass、执行步进和系统状态。
- `src/eraengine/GameView` 负责控制台模型、输入表面和状态展示。
- `src/platform` 负责窗口、托盘和平台集成；`src/qml` 负责视图组合；`src/tools` 负责协议工具。

## 关键口径

变量解析只有一条顺序：段身份作用域由装载期写入 `LogicalLine::scopeKey`，运行时显式 intern 为 `ScopeId`；`VariableNode` 的 `AccessKind/VariableKey` 或 `VariableStorage::resolve*Locus` 决定位置，读写共享解析结果。

存档分成两个边界。`08-variable-storage.puml` 描述 `erasave::Map` 如何把声明映射到条目和存储槽；`11-save-format.puml` 只描述 `EraBinaryData::Version`、编码/解码和不兼容文件的原子拒绝；`05-runtime-save-load.puml` 描述状态机和执行引擎怎样调用它们。私有写盘键由 `makePrivateStorageKey(segment, name)` 统一构造，图中不再声称文件只保存裸名。

静态检查发现的未定义函数属于内容/脚本诊断。运行时不会伪造 `@GAMEOVER` 实现；如果脚本在执行时调用缺失标签，仍按执行错误处理。

## 渲染与检查

```sh
plantuml -tpng docs/uml/*.puml
plantuml -tsvg docs/uml/*.puml
```

当前环境使用 PlantUML 的 `smetana` 布局，因此不依赖 Graphviz。只检查语法而不提交生成的图片时，可使用：

```sh
for f in docs/uml/*.puml; do plantuml -pipe -tsvg < "$f" >/dev/null || exit 1; done
```

可用 `rg -n 'class |signal|Q_PROPERTY|saveTo|loadFrom|runSlice|onFramePresented' src/eraengine src/qml` 对照图中的接口。图中文字只保留能从当前源码确认、且对边界有帮助的事实；历史性能记录、重复实现说明和已删除的装载阶段放在版本历史中，不放在结构图索引中。
