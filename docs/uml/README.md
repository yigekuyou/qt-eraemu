# emuera UML（当前实现）

PlantUML 源码 + 渲染产物。所有图都是**当前代码**的静态结构，不包含计划中的设计。

| 文件 | 类型 | 内容 |
| --- | --- | --- |
| `00-overview.puml` | 组件图 | 分层总览：装载期一次定型 → 运行期只做存取 |
| `01-load-classes.puml` | 类图 | 装载期：`EraEngine` / `ErbLoader` / `AstBuilder` / `EraParseTable` / 变量表与配置 / 收尾 6 步 |
| `02-runtime-classes.puml` | 类图 | 运行期：`ExecutionEngine` / `ScriptRunner` 单步状态机 / `ProcessState` / `LoopFrame` |
| `03-expression-classes.puml` | 类图 | 表达式 AST、`AccessKind` 访问描述符、`ExpressionEvaluator` 两道守卫、`VariableStorage` |
| `04-load-pipeline.puml` | 时序图 | 装载流水线：CSV → 逐文件解析 → `markLoadComplete` 1..6(+7) → 诊断 |
| `05-variable-access.puml` | 活动图 | 运行期变量访问决策流：作用域守卫 → 动态 → 维数守卫 → 快路径 |
| `06-diagnostics.puml` | 类图 | 诊断模型：`ParseDiagnostics` / `ParseDiagnostic` / `DiagCode` 全码与产生步骤 |
| `07-static-checks.puml` | 卡片图 | `checkStaticValues` 检查目录（C1..C4）与已验证口径 |

同目录的 `.png` / `.svg` 是渲染结果，可直接查看。

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

- 快路径两道守卫：作用域不符 → `Dynamic`；裸键下标数与 `AccessKind` 维数不符 → 回退旧路（旧路会 throw）。
  私有复合键（函数名 + `0x1F` + 名字）保持旧路的宽松 1 维读。
- `writeLoopCounter` 写的是「计数器固定下标上的循环值」——值是值，不是下标，因此没有「循环值越界」检查。
- `checkStaticValues` 只产出诊断、不改变运行行为；`validateExpression` 类错误才会置 `line.isError`。
