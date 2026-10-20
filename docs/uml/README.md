# emuera UML（当前实现）

PlantUML 源码。所有图都是**当前代码**的静态结构，不包含计划中的设计。
渲染产物（`.png` / `.svg`）是构建输出，`.gitignore` 里已排除，不入库。

| 文件 | 类型 | 内容 |
| --- | --- | --- |
| `00-overview.puml` | 组件图 | 分层总览：装载期一次定型 → 运行期只做存取 |
| `01-load-classes.puml` | 类图 | 装载期：`EraEngine` / `ErbLoader` / `AstBuilder` / `EraParseTable` / 变量表与配置 / 收尾 pass 表 |
| `02-runtime-classes.puml` | 类图 | 运行期：`ExecutionEngine` / `ScriptRunner` 单步状态机 / `ProcessState` / `LoopFrame`（存取成员只剩转调） |
| `03-expression-classes.puml` | 类图 | 表达式 AST、`AccessKind` 访问描述符、`ExpressionEvaluator` 唯一分派、`VariableStorage` 唯一按名原语 |
| `04-load-pipeline.puml` | 时序图 | 装载流水线：CSV → 逐文件解析 → `kLoadPasses[7]` 表驱动收尾 → 诊断 |
| `05-variable-access.puml` | 活动图 | 运行期变量访问：两条入口 → 唯一分派（作用域守卫 + `AccessKind`）→ 唯一按名原语 |
| `06-diagnostics.puml` | 类图 | 诊断模型：`ParseDiagnostics` / `ParseDiagnostic` / `DiagEffect` / `DiagCode` 全码与产生步骤 |
| `07-static-checks.puml` | 卡片图 | `checkStaticValues` 检查目录（C1..C4）与已验证口径 |

同目录的 `.png` / `.svg` 是渲染结果（本地生成，不入库），按下面命令随时重渲染。

## 渲染

```bash
plantuml -tpng docs/uml/*.puml      # 位图
plantuml -tsvg docs/uml/*.puml      # 矢量
```

源码里带 `!pragma layout smetana`：本机没有 graphviz（`dot`），只能用 PlantUML 内置的 smetana
布局引擎。装上 graphviz 后删掉该行（以及上一行注释）即可得到更好的自动布局。

活动图（`05-variable-access.puml`）另有 `!pragma useVerticalIf on`，让 if/elseif 竖直展开；
旧版本 PlantUML 忽略该行也能渲染，只是分支会横向铺开。

## 与 Umbrello 的关系

Umbrello 不能导入 PlantUML，只认 XMI。PlantUML 可导出类图 XMI：

```bash
plantuml -txmi docs/uml/01-load-classes.puml
```

但导出的 XMI 只含类/属性/操作，**不含关联与图形布局**（UML 1.3 / XMI 1.1，Umbrello 打开后需重新排布）。
需要完整可编辑模型时，用 Umbrello 手工建立或用 Umbrello 自己保存的 XMI 作为模板。
本机 `umbrello6 --export` 在无头环境下会崩溃（KCrash / SIGSEGV），只能在其 GUI 中打开确认。

## 口径提醒（图中标注）

- **单一分派**：读写各只有一条入口 `ExpressionEvaluator::readIntNode` / `writeIntNode`（作用域守卫 +
  `AccessKind` 分派）；`ScriptRunner` 的 FOR/NEXT 与表达式求值共用它，不再各写一份 `switch`。
- **单一按名原语**：`VariableStorage::readIntByName` / `writeIntByName` 是「名字 + 下标 → 存储」的唯一实现；
  `ExecutionEngine::writeLhs`、`ScriptRunner::readIntVar` / `writeIntVar`、求值器旧路尾部全部转调它
  （顺序：形参 → 角色 → ARG/LOCAL → ARGS/LOCALS/RESULTS → 系统变量 → 2D/3D → 1D）。
- 快路径守卫：作用域不符 → `Dynamic`；裸键下标数与 `AccessKind` 维数不符 → 按名原语（其行为即旧路 throw）。
  私有复合键（函数名 + `0x1F` + 名字）保持宽松 1 维读。
- `writeLoopCounter` 写的是「计数器固定下标上的循环值」——值是值，不是下标，因此没有「循环值越界」检查。
- **收尾 pass 表**：`kLoadPasses[7] = { label, fn, optional, readOnly }`，表即流程（顺序 = 执行顺序，
  日志文案也取自表）。4/5/6(+7) 标记 `readOnly` 且为 public，可脱离 `markLoadComplete` 单独调用；
  重复调用会重复产生诊断。
- **诊断顺序可复现**：装载并行 + `QHash` 随机化哈希种子会让诊断的*产生顺序*在进程间漂移
  （条数与内容不变）。收尾末尾 `ParseDiagnostics::sortForOutput()` 按 `(文件, 行, 列, code, message)`
  稳定排序一次，文本视图同步重建 —— `dump_warnings` 的基线比对才立得住。
- **诊断两种后果**：`DiagEffect` 与 `DiagSeverity` 正交。`RejectLine`（该行失效）当前只有 6 处，
  全在 3/6 `finalizeValidation`；`checkStaticValues` 全部 `ReportOnly` —— 静态检查不允许改变运行行为，
  这条不变式由字段表达，不再只靠注释。
