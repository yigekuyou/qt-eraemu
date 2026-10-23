/*
 * emuera —— Emuera（ERB 脚本引擎）的 Qt6 + QML/C++ 移植
 * Copyright (C) 2026  yigekuyou
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef ERAENGINE_GAMEPROC_POSITION_STATE_H
#define ERAENGINE_GAMEPROC_POSITION_STATE_H

#include <QList>
#include <QString>

#include "era_parse_table.h"   // PositionState / PositionEffects / ScriptView / Frame

// ---------------------------------------------------------------------------
// 位置区：执行位置（内存空间 + PC）与调用栈的**状态转移**
//
// 本文件不认识 EraParseTable、不 emit 任何信号、不认识装载阶段：
//   * 状态  = PositionState（纯数据，见 era_parse_table.h）
//   * 输入  = ScriptView（两张表的指针：脚本表 + 全局标签索引）
//   * 输出  = PositionEffects（「对外要广播哪几位」的布尔集）
// 于是「跳转/调用/返回」的语义可以用无 QObject 的单元测试直接钉死
//（见 test_position_state.cpp）；信号由 EraParseTable 的壳子按下面的顺序发。
//
// ---- 契约：失败 = 完全不动状态 ----
// 所有返回 bool 的操作在返回 false 时**不改 PositionState、不亮任何 Effects 位**。
// 唯一例外写在各自的注释里（没有例外：旧实现的早退分支都不写状态）。
//
// ---- 契约：emit 顺序（壳子按 Effects 逐位发，顺序写死）----
//   规范顺序：callStackChanged -> memorySpaceChanged -> positionChanged -> jumpRequested
// 逐方法对照旧实现：
//   callLabel / callLabelWithReturn / jumpLabel / callLabelAt
//                                   = 压帧(callStack) -> 切空间(memorySpace) -> 定位(position)
//   returnFromCall                  = 弹帧 -> 切空间 -> 定位
//   jumpToLabel                     = (解析时可能切空间) -> 定位 -> jumpRequested
//   setPosition                     = 切空间 -> 定位
//   reset                           = callStack -> position
// 全部符合规范顺序。**唯一的例外不在本文件里**：EraParseTable::clear() 旧代码先
// emit positionChanged(QString(), 0) 再 emit callStackChanged(0)（与 reset 相反），
// 那一处由 clear() 自己手工按旧顺序发，不经过 Effects。
//
// 同一方法里若 memorySpaceChanged 与 positionChanged 同时亮，旧实现里切空间一定
// 在定位之前（switchToMemorySpace 都在 setCurrentLineInternal 之前调用），与规范
// 顺序一致；jumpToLabel 的切空间发生在解析（resolveJumpTarget）阶段，也在定位之前。
// ---------------------------------------------------------------------------
namespace position {

// ---- 基元（私有语义，公开以便直接测；EraParseTable 不直接调它们，除了 reset）----

// 写 PC。forceEmit=true 时即使行号没变也亮 positionChanged（旧
// setCurrentLineInternal(line, true)：跳转/调用落到同一行也必须广播）。
// 先作废作用域缓存（**在早退之前**，与旧实现同序），再把负数钳到 0。
// line 未变且 forceEmit=false -> 什么都不亮（旧实现的 early-return）。
void setLineInternal(PositionState& state, int line, bool forceEmit, PositionEffects& fx);

// 压帧：callStack.append + 亮 callStackChanged（栈深只由 callStack 决定）。
void pushFrame(PositionState& state, const Frame& frame, PositionEffects& fx);

// 弹帧：空栈返回 Frame() 且不动状态（不亮位）；否则 takeLast + 亮
// callStackChanged，返回被弹出的帧。
Frame popFrame(PositionState& state, PositionEffects& fx);

// 某脚本的行数（不存在 = 0）。跳转越界判定的唯一数据来源。
int lineCount(const ScriptView& view, const QString& script);

// 切内存空间：同名或空名 = no-op（不亮位）；否则改 script 并亮 memorySpaceChanged。
// 注意：这里**不清栈、不动 PC** —— 调用方随后自己定位。
void switchToMemorySpace(PositionState& state, const QString& script, PositionEffects& fx);

// 复位：清栈 + line=0。**不清 script**（装载期 clear() 要单独清）——
// 旧 resetPosition() 的语义。也不动 jumped / 作用域缓存（旧实现同样不动）。
// 返回要发的两位（callStack + position）；调用方按需丢弃（见 clear()）。
PositionEffects reset(PositionState& state);

// ---- 公开操作（与 EraParseTable 的 public 方法一一对应）----

// 切换脚本（若非空且不同）+ 写 jumped + 强制定位（forceEmit）。
// view 不参与：本操作不需要解析任何标签/行数（保留参数只为与其它操作同形）。
// 亮位：memorySpaceChanged?、positionChanged。
void setPosition(PositionState& state, const ScriptView& view, const QString& script,
                 int line, bool jumped, PositionEffects& fx);

// 顺序推进一行：jumped=false，PC+1（line 一定变化 -> 亮 positionChanged；
// 但 PC+1 后若与旧值相等是不可能的，除非旧 PC 是负数 —— setLineInternal 会钳）。
void advance(PositionState& state, PositionEffects& fx);

// 跳到本脚本的某行：line < 0 或 line >= 行数（且行数 > 0）时返回 false 且不动状态。
// 成功：jumped=true + 强制定位。亮位：positionChanged。
bool jumpToLine(PositionState& state, const ScriptView& view, int line, PositionEffects& fx);

// 按标签跳转（当前脚本优先，未命中则全脚本扫描并在命中的脚本上切空间）。
// 成功：jumped=true + 强制定位 + 亮 jumpRequested（壳子用它发 jumpRequested 信号）。
// 亮位：memorySpaceChanged?、positionChanged、jumpRequested。
bool jumpToLabel(PositionState& state, const ScriptView& view, const QString& label,
                 PositionEffects& fx);

// CALL：压帧（返回地址 = 当前行 +1；advanceWasCalled=true 时 = 当前行，因为
// 调用方已经把 PC 推过了当前指令）+ 切空间 + 强制定位。
// 无法解析目标 -> false，不压帧。
// 亮位：callStackChanged、memorySpaceChanged?、positionChanged。
bool callLabel(PositionState& state, const ScriptView& view, const QString& label,
               bool advanceWasCalled, PositionEffects& fx);

// 同上，但返回地址由调用方显式给出（执行链同步调用用户函数时用）。
bool callLabelWithReturn(PositionState& state, const ScriptView& view, const QString& label,
                         int returnLine, PositionEffects& fx);

// 以「被调脚本内的行号」直接调用（事件分组导航）：压帧的 callLabel 取自
// 目标行的 labelName（原样，与旧实现一致），entryLine = 该行号。
// line 越界 -> false 且不压帧、不动状态。
// 亮位：callStackChanged、memorySpaceChanged?、positionChanged。
bool callLabelAt(PositionState& state, const ScriptView& view, const QString& script, int line,
                 const QString& returnScript, int returnLine, PositionEffects& fx);

// JUMP / TRYJUMP：跳转但不产生新返回地址。
//   空栈（顶层）-> 退化为 jumpToLabel；
//   非空栈     -> 压一帧 isJump=true，其 (script, returnLine) 继承**当前栈顶**的
//                 返回地址（对齐 C# CalledFunction.IsJump + ProcessState.Return 的
//                 「JUMP 帧弹出后立刻递归 Return」效果：JUMP 目标函数 RETURN 时
//                 直接回到当前函数的调用者）。
// 亮位：空栈时同 jumpToLabel；非空栈时 callStackChanged、memorySpaceChanged?、
//       positionChanged（**不亮 jumpRequested** —— 旧实现也不发）。
bool jumpLabel(PositionState& state, const ScriptView& view, const QString& label,
               PositionEffects& fx);

// RETURN：空栈 -> false 且不动状态。否则弹帧 -> 切回 frame.script（非空且不同）
// -> jumped=false + 强制定位到 frame.returnLine。
// view 不参与（返回地址完全来自帧）。
// 亮位：callStackChanged、memorySpaceChanged?、positionChanged。
bool returnFromCall(PositionState& state, const ScriptView& view, PositionEffects& fx);

// ---- 只读查询 / 解析 ----

// 「裸标签属于哪个脚本」的**唯一**入口，也是**唯一**的择一规则：
// 在唯一那张索引（ScriptView::labelIndex）里取全序第一条满足条件的声明。
//   callableOnly = true  只收 @函数标签（CALL / JUMP / 式中调用 / 存在性探针）
//   callableOnly = false @ 与 $ 都收（GOTO 解析、入口点挑选）
//   onlyScript 非空      只在该脚本内找（CALL 的「当前脚本优先」）
// 全序 = 脚本名 QString::compare 升序 -> 同脚本内行号升序（labelRefLess，
// 装载收尾 1/6 的 finalizeLabelIndex 固化）。重名时**一律取第一条** ——
// 与 C# LabelDictionary.SortLabels 的 list[0] 同义。跨脚本那一跳与旧
// labelLists 的「排序后第一份」逐字一致；同一脚本内的重复声明则从旧实现
// labelPositions 的「最后一条」改成「第一条」（C# 也是第一条）。
// 返回的指针指向索引内部（view 必须活得比它久）。
const LabelRef* firstLabel(const ScriptView& view, const QString& name, bool callableOnly,
                           const QString& onlyScript = QString());

// GOTO 的 $ 标签解析（C# LabelDictionary.GetLabelDollar(func.ParentLabelLine)）：
//   * 只认 $标签（ref.isFunction == false）—— GOTO 到 @ 是另一条路径；
//   * 只认**同一函数段**：ref.functionLine == functionSegmentLine（段身份在装载期
//     写进 LabelRef，见那里注释）-1 = 第一个 @标签之前的文件头段；
//   * 只认本脚本（段身份是脚本内的行号，跨脚本没有可比性）。
// 顺序取索引里该名字的第一条命中 = 同脚本内行号最小的一条（全序固化后成立），
// 与旧的「从函数入口行往下扫、第一个匹配」逐字一致，但不再扫行。
const LabelRef* firstGotoLabel(const ScriptView& view, const QString& name,
                               const QString& script, int functionSegmentLine);

// 解析 CALL/JUMP 的目标：当前脚本优先（且必须是 @函数标签），否则查同一张
// 索引的全序第一条。两次查询走的是同一个 firstLabel，规则不在这里。
// 「必须是 @函数标签」这一条不是性能优化，而是语义：
//   * 旧实现是 for(m_scripts) 全量扫描 + 每脚本一次 label.toUpper() ——
//     1500+ 脚本的语料上每次跨文件 CALL/式中调用都要 ~0.7ms（eraTW 地图逐格
//     TRYCALLFORM 一次上万次；CALC_COS 这类小函数的式中调用正是这 1541 次迭代
//     的价钱）。
//   * 命中 $GOTO 标签会导致自跳死循环：C# 的 FunctionIdentifierDictionary 只收
//     @label；eraMegaten 的 SYSTEM_TITLE.erb:127 `$PRINT_TITLE` 与
//     TITLE_STOCK.ERB:17 `@PRINT_TITLE` 同名共存，CALL 命中 $ 就死循环。
//     callableOnly 把这一条变成了索引查询的结构性事实。
// 不改任何状态（当前脚本的优先判定只看 state.script）。
bool resolveCallableLabel(const PositionState& state, const ScriptView& view, const QString& label,
                         QString& targetScript, int& targetLine);

// GOTO 目标解析：先本脚本，未命中则查同一张索引的全序第一条；
// 命中时**切到那个脚本**（亮 memorySpaceChanged）。position 只在成功时写入。
bool resolveJumpTarget(PositionState& state, const ScriptView& view, const QString& label,
                       int& position, PositionEffects& fx);

// 指定脚本内的标签解析；成功时切到该脚本（同名也 no-op）。
bool resolveJumpToScript(PositionState& state, const ScriptView& view, const QString& script,
                         const QString& label, int& position, PositionEffects& fx);

// 当前执行行所属的**段身份**（eravar::ScopeId；kGlobalScope = 顶层）。
// 直接读 LogicalLine::scopeKey（装载期写入的 (脚本名, 段起点) 文本）再 intern；
// **没有任何按函数名的回查**。装载期尚未开始执行（无位置/越界）时返回
// kGlobalScope —— 此时只有广域常量可见，正是 #DIM 维数之类查询想要的语义。
// 只读位置：每次调用都按当前行现算（不缓存）。
eravar::ScopeId executionScopeId(const PositionState& state, const ScriptView& view);

}  // namespace position

#endif  // ERAENGINE_GAMEPROC_POSITION_STATE_H