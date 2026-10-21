# emuera UML（当前实现）

PlantUML 源码。所有图都是**当前代码**的静态结构，不包含计划中的设计。
渲染产物（`.png` / `.svg`）是构建输出，`.gitignore` 里已排除，不入库。

| 文件 | 类型 | 内容 |
| --- | --- | --- |
| `00-overview.puml` | 组件图 | 分层总览：装载期一次定型 → 运行期只做存取；只读收尾独立成 `parsecheck` |
| `01-load-classes.puml` | 类图 | 装载期：`EraEngine` / `ErbLoader` / `AstBuilder` / `EraParseTable` / `ParseIrView` 与 4/5/6 自由函数 / `LoadPhase` 阶段机 / 收尾 pass 表 |
| `02-runtime-classes.puml` | 类图 | 运行期：`ExecutionEngine` / `ScriptRunner` 单步状态机 / `ProcessState` / `LoopFrame`（作用是显式 `ScopeId`） |
| `03-expression-classes.puml` | 类图 | 表达式 AST、`AccessKind` 16 值、`VariableKey{scopeId,nameId}`、名字 intern 表、`resolverTable` |
| `04-load-pipeline.puml` | 时序图 | 装载流水线：CSV → 逐文件解析 → `kLoadPasses[7]` 表驱动收尾 → 诊断 |
| `05-variable-access.puml` | 活动图 | 运行期变量访问：两条入口 → 一张 resolver 表 → 唯一按名原语 |
| `06-diagnostics.puml` | 类图 | 诊断模型：`ParseDiagnostics` / `ParseDiagnostic` / `DiagEffect` / `DiagCode` 全码与产生步骤 |
| `07-static-checks.puml` | 卡片图 | `checkStaticValues` 检查目录（C1..C4）与已验证口径 |
| `08-save-load.puml` | 时序图 | 存档 / 读档：`erasave::Map`（条目 ↔ 身份的唯一一张表）、`SaveReadiness` 前置条件 |

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

- **变量身份是整数，不是字符串**：`VariableKey { scopeId, nameId }`（名字用 intern 表换成 `quint32`）。
  上一版把私有名编成 `函数名 + 0x1F + 名字`，而 `computeVariableAccess` 与 `setPrivateScope`
  各拼一次、必须逐字节一致 —— 任何拼写/分隔符疏漏都表现为「某个变量悄悄变成另一个变量」。
  现在这个拼接只在 `makePrivateStorageKey` 一处，比较是 `quint32` 相等。
- **装载期全定型**：`AccessKind` 16 值。封闭 5 类（形参 / 伪变量 / 系统槽 / 角色 / 常数）
  各有 kind；`usesKey(kind)` 标明哪些 kind 消费 `accessKey`（守卫的适用范围本身是数据）。
  真实语料里 `Dynamic` 只剩「有系统元数据但无系统存储槽」的裸名：eraTW 0.5% / FL3.8 0.3%。
- **分派是一张表**：`ExpressionEvaluator::resolverTable()` 20 行，顺序与旧 `if` 链逐块对应
  （`kinds` 位掩码 + `needsKey` + 读/写处理函数）。`readIntNode` / `writeIntNode` 是读写各一条入口，
  FOR/NEXT 与表达式求值共用，不再各自实现一份 `switch`。
- **作用域是参数**：`eravar::ScopeId` 由 `ScriptRunner::m_currentScopeId`（换函数时更新一次）
  和 `ExecutionEngine::m_activeScope` 显式下传；求值器不读 storage 里的隐含状态。
  键自带作用域，所以**没有「作用域守卫」**。只剩一条：裸键下标数与维数不符 → 按名原语
  （其行为即旧路 `throw`）；私有键保持宽松 1 维读。
- **单一按名原语**：`VariableStorage::readIntByName` / `writeIntByName(name, indices, scope)` 是
  「名字 + 下标 → 存储」的唯一实现（顺序：形参 → 角色 → ARG/LOCAL → ARGS/LOCALS/RESULTS →
  系统变量 → 2D/3D → 1D）。存储容器仍是字符串键（存档格式不变），id → 串只在 `storageKeyFor` 一处。
- `writeLoopCounter` 写的是「计数器固定下标上的循环值」——值是值，不是下标，因此没有「循环值越界」检查。
- **收尾 pass 表**：`kLoadPasses[7] = { label, fn, gate, phaseAfter }`，表即流程（顺序 = 执行顺序，
  日志文案也取自表）。`gate` 是「受哪个开关门控」（`Always` / `NotCalledWarning`），
  驱动逻辑里只有 `isGateOpen()` 一处按它求值；`phaseAfter` 让阶段推进也是数据而不是分支。
  4/5/6(+7) 是只读步骤，可脱离 `markLoadComplete` 单独调用。
- **装载阶段是显式状态机**：`LoadPhase { Empty, Loading, Bound, Finalized }`。每个公开入口声明它要求的
  阶段；越界调用 = `qWarning` + no-op（不 assert —— release 下也必须安全，且诊断容器不变）。
  只读步骤的重复调用是**可观察**契约：`readOnlyPassRunCount(pass)` 记录次数，重复调用会重复产生诊断。
- **私有名表在装载期建立**：`VariableTable::buildPrivateNameTable()` 由声明（`scope == Local`）一次建好，
  `finalizeBindings()` 末尾 `installPrivateScopeTable()` 装进存储层。安装后 `setPrivateScope` 只写
  当前帧（无 insert / reserve / 循环），正确性不再依赖调用方把 `names` 传全。
- **只读收尾已搬出 God class**：`parsecheck::resolveStaticTargets / checkStructures / checkStaticValues`
  接收全 `const` 的 `ParseIrView`，输出只有 `ParseDiagnostics`；`EraParseTable` 只剩一行 forwarder。
  `test_parse_checks` 绕过 `EraParseTable` 直接调用，并与 forwarder 的结果逐字段（含产生顺序）比对。
  1/2/3（会改装载状态）仍在 `EraParseTable` 里 —— 这是当前的分界线。
- **存档有独立的键映射层**：`erasave::Map`（`save_variable_map.{h,cpp}`）是「存档条目 ↔ 变量身份」的
  唯一一张表，`Entry = { name, key, slot, dimensions, user, persisted }`。文件里写的是**裸名**，
  身份不进文件；存档层不再认识 `FUNC\x1fNAME` 这种键形态，也不必改那 31 个字符串键容器。
  前置条件由 `SaveReadiness`（`Ready` / `NoDeclarationTable` / `DeclarationsNotResolved`）显式回答。
- **诊断顺序可复现**：装载并行 + `QHash` 随机化哈希种子会让诊断的*产生顺序*在进程间漂移
  （条数与内容不变）。收尾末尾 `ParseDiagnostics::sortForOutput()` 按 `(文件, 行, 列, code, message)`
  稳定排序一次，文本视图同步重建 —— `dump_warnings` 的基线比对才立得住。
- **诊断两种后果**：`DiagEffect` 与 `DiagSeverity` 正交。`RejectLine`（该行失效）当前只有 6 处，
  全在 3/6 `finalizeValidation`；`checkStaticValues` 全部 `ReportOnly` —— 静态检查不允许改变运行行为，
  这条不变式由字段表达，不再只靠注释。

## 回归基线

`dump_warnings` 五语料（`eraTW` / `eraMegaten` / `eraTetris` / `FL3.8` / `ecd`）：
**6 / 0 / 0 / 0 / 0** 行，逐行可比（`sort` 后 `diff`）。
累计六个重构（身份整数化 / 全定型 / 作用域显式化 / 只读收尾外移 / 存档独立映射 /
私有名表前移）前后完全一致；`ctest` 48 → 52（新增 `test_parse_checks` /
`test_save_variable_map` / `test_private_scope_table` / `test_load_contracts`）。

存档字节：`test_save_variable_map` 的 golden 来自**固定 `QT_HASH_SEED=0`** 的旧实现输出 ——
用户 SAVEDATA 条目顺序取自 `QHash` 遍历，Qt 每进程随机播种，所以不固定播种时「逐字节相同」
本身不可判定（重构前连跑三次就是三个 md5）。