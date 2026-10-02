# test/ —— era 全函数覆盖示例与回归

本目录是 **emuera（Qt/C++ 移植）** 的端到端回归测试：一个可运行的 era 游戏
（`test/example/`），逐项覆盖 Emuera 的命令与式中函数，并对关键语义做断言。

设计原则：**用例清单来自 C# 权威源码**（Emuera 原版 + EmueraEE），
不依赖本移植的 C++ 表 —— 这样才能反过来发现移植缺了什么。

## 目录

| 路径 | 作用 |
| --- | --- |
| `example/` | 可运行的 era 游戏（CSV + ERB），测试载体 |
| `example/ERB/0N_*.ERB` | 手写测试组（语义断言） |
| `example/ERB/10_COVERAGE.ERB` | **自动生成**的全函数冒烟覆盖（勿手改） |
| `data/emuera_standard_cmds.txt` | Emuera 原版命令清单（导出自 C#） |
| `data/emuera_standard_funcs.txt` | Emuera 原版式中函数清单（导出自 C#） |
| `data/emuera_ee_cmds.txt` | EmueraEE 扩展命令清单 |
| `data/coverage_report.txt` | 覆盖率报告（生成） |
| `run_example.sh` | 运行示例（唯一需要的入口） |
| `export_command_tables.py` | 从 C# 源码导出上述命令清单 |
| `gen_coverage.py` | 依据清单生成覆盖组 + 覆盖率报告 |

## 快速开始

```bash
# 1) 编译（含测试用 CLI）
cmake --build build --target test_cli

# 2) 运行全部（组1..10、14 + 汇总）
./test/run_example.sh

# 3) 只跑某一组
./test/run_example.sh 5     # TRY*LIST + FUNC/ENDFUNC
./test/run_example.sh 11    # 输入族（需 test_cli 自动喂输入）
```

退出码：`0` = 渲染自检无异常 **且** 全部断言通过；非 0 = 有失败。

## 测试组

| 组 | 内容 |
| --- | --- |
| 1 | 变量 · 赋值 · 数组 · 常量 |
| 2 | PRINT 全族 · 按钮 · 颜色 · 对齐 · REDRAW · HTML 系 |
| 3 | 控制流 IF/SIF/SELECTCASE/循环/GOTO |
| 4 | 调用 CALL/CALLFORM/TRYCALL/JUMP 系/CALLF/RETURN |
| 5 | **TRYCALLLIST / TRYJUMPLIST / TRYGOTOLIST / GOTOLIST + FUNC/ENDFUNC** |
| 6 | 字符串函数 |
| 7 | 数值函数 · 随机数控制 |
| 8 | CSV / 角色函数 · 角色列表操作 |
| 9 | 存档 SAVE/LOAD/DEL/CHK · SAVEGLOBAL · SAVETEXT/LOADTEXT · SAVECHARA/LOADCHARA |
| 10 | **全函数覆盖**（依据 C# 命令表自动生成的冒烟段） |
| 11 | 输入族 · 等待（`INPUT`/`INPUTS`/`ONEINPUT`/`TINPUT`/`WAITANYKEY`/`AWAIT`…） |
| 12 | `BEGIN`（破坏性：切换流程、不返回） |
| 13 | `THROW`（破坏性：主动报错终止，**预期**「执行出错」） |
| 14 | 剩余命令（`TRYGOTO`/`JUMPFORM`/`TRYCJUMPFORM`/`PRINT_RECT`/`ADDDEFCHARA`/`CALLEVENT`…） |

组 11/12/13 不在「全部自动运行」路径里：11 需要外部喂输入，12/13 会中断或改流程。

## 关于「自动输入」

`INPUT` / `INPUTS` / `ONEINPUT` / `TINPUT` / `WAITANYKEY` / `AWAIT` 在 GUI 下会
挂起等玩家点击，**只有 `test_cli` 能用 `--script` 序列自动喂入**。这正是
`run_example.sh` 只能配 `test_cli` 跑完整流程的原因：

```bash
./build/src/eraengine/test_cli test/example --script 0,0,0,0,0,0 --frames 400 --check
```

## 覆盖率（依据 C# 表）

```bash
python3 test/export_command_tables.py   # C# 源码 -> data/*.txt
python3 test/gen_coverage.py            # 生成 10_COVERAGE.ERB + 报告
python3 test/gen_coverage.py --check    # 只出报告
```

命令清单来自：

* **Emuera 原版**：`Emuera/GameProc/Function/BuiltInFunctionCode.cs`（`enum FunctionCode`）
  与 `Emuera/GameData/Function/Creator.cs`（`methodList`）；
* **EmueraEE 扩展**：EE 发行版自带文档（`eraTW/README集/EmueraEE Readme/`）里的
  新增命令（EE 未附带 C# 源码，清单见 `export_command_tables.py` 的 `EE_EXTENSIONS`）。

当前覆盖（详见 `data/coverage_report.txt`）：原版命令 199 条、式中函数 163 条、
EE 扩展 56 条，**全部 0 未覆盖**。少量命令刻意不由自动段执行（声音/文本框/多列/
内存等需要运行环境，或会改流程），报告中逐条给出原因。

## 相关

* 编译器/引擎回归：`cd build && ctest`（注意：`test_input` / `test_statements`
  在本移植**基线**上即有失败，与测试示例无关）。
* 本目录的改动同时记录了若干引擎修复：`CHKDATA` 返回值（EraDataState）、
  `RESETDATA` 清空角色、`RESETGLOBAL` 保留函数私有变量、`QUIT` 结束程序、
  `SAVETEXT/LOADTEXT` 的 `txt{nn}.txt` 语义、`CHKFONT` 无 GUI 时的崩溃等。
