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
// ---------------------------------------------------------------------------
// position_state.cpp —— 位置区的状态转移（逐字搬运解析表里的旧实现）
//
// 这里没有任何 emit、不持有任何 QObject、没有装载阶段：只读写 PositionState、
// 只读 ScriptView、只置 PositionEffects 的位。旧实现每一次 emit 对应这里亮一位，
// 顺序语义由「调本文件的壳子」写死（见 position_state.h 与 era_parse_table.h 的
// emitEffects 契约）。
// ---------------------------------------------------------------------------
#include "position_state.h"

#include "ast/name_interner.h"   // eravar::internName / kGlobalScope

namespace position {

namespace {

// 脚本表查表（旧 script() 的语义：不存在 = nullptr）。
const ScriptData* findScript(const ScriptView& view, const QString& script) {
    if (!view.scripts) return nullptr;
    const auto it = view.scripts->constFind(script);
    return it == view.scripts->constEnd() ? nullptr : &it.value();
}

// 旧 getLabelPosition 的语义：脚本不存在 = -1；标签按大写折叠查表。
int labelPosition(const ScriptData* data, const QString& label) {
    if (!data) return -1;
    return data->labelPositions.value(label.toUpper(), -1);
}

// 旧 lineAt 的语义：越界/不存在 = nullptr。
const LogicalLine* lineAt(const ScriptView& view, const QString& script, int line) {
    const ScriptData* data = findScript(view, script);
    if (!data || line < 0 || line >= data->lines.size()) return nullptr;
    return &data->lines.at(line);
}

}  // namespace

// ---------------------------------------------------------------------------
// 基元
// ---------------------------------------------------------------------------

void setLineInternal(PositionState& state, int line, bool forceEmit, PositionEffects& fx) {
    // 作用域缓存按「当前行所属函数」记忆：行变了就必须作废。
    // 注意顺序：旧实现先作废缓存、再判早退（同一行跳到同一行也要作废，
    // 因为缓存可能因为别的原因陈旧）。
    state.scopeIdValid = false;
    if (line < 0) {
        line = 0;
    }
    if (line == state.line && !forceEmit) {
        return;   // 旧实现的 early-return：什么都不发
    }
    state.line = line;
    fx.positionChanged = true;
}

void pushFrame(PositionState& state, const Frame& frame, PositionEffects& fx) {
    state.callStack.append(frame);
    state.depth = state.callStack.size();
    fx.callStackChanged = true;
}

Frame popFrame(PositionState& state, PositionEffects& fx) {
    if (state.callStack.isEmpty()) {
        return Frame();
    }
    const Frame frame = state.callStack.takeLast();
    state.depth = state.callStack.size();
    fx.callStackChanged = true;
    return frame;
}

int lineCount(const ScriptView& view, const QString& script) {
    const ScriptData* data = findScript(view, script);
    return data ? data->lines.size() : 0;
}

void switchToMemorySpace(PositionState& state, const QString& script, PositionEffects& fx) {
    if (state.script == script || script.isEmpty()) {
        return;
    }
    state.script = script;
    fx.memorySpaceChanged = true;
}

PositionEffects reset(PositionState& state) {
    // 与旧 resetPosition() 逐字一致：清栈 + PC=0，**不动 script / jumped /
    // 作用域缓存**。script 由调用方决定（clear() 要清、setEntryPoint 要保留）。
    state.callStack.clear();
    state.depth = 0;
    state.line = 0;
    PositionEffects fx;
    fx.callStackChanged = true;
    fx.positionChanged = true;
    return fx;
}

// ---------------------------------------------------------------------------
// 公开操作
// ---------------------------------------------------------------------------

void setPosition(PositionState& state, const ScriptView&, const QString& script,
                 int line, bool jumped, PositionEffects& fx) {
    if (!script.isEmpty() && script != state.script) {
        switchToMemorySpace(state, script, fx);
    }
    state.jumped = jumped;
    setLineInternal(state, line, /*forceEmit=*/true, fx);
}

void advance(PositionState& state, PositionEffects& fx) {
    state.jumped = false;
    setLineInternal(state, state.line + 1, /*forceEmit=*/false, fx);
}

bool jumpToLine(PositionState& state, const ScriptView& view, int line, PositionEffects& fx) {
    const int count = lineCount(view, state.script);
    if (line < 0 || (count > 0 && line >= count)) {
        return false;
    }
    state.jumped = true;
    setLineInternal(state, line, /*forceEmit=*/true, fx);
    return true;
}

bool jumpToLabel(PositionState& state, const ScriptView& view, const QString& label,
                 PositionEffects& fx) {
    int target = -1;
    // 注意：解析本身可能切内存空间（跨脚本标签），旧实现也是这样 ——
    // 那个 memorySpaceChanged 发生在定位之前，与规范 emit 顺序一致。
    if (!resolveJumpTarget(state, view, label, target, fx)) {
        return false;
    }

    state.jumped = true;
    setLineInternal(state, target, /*forceEmit=*/true, fx);

    fx.jumpRequested = true;
    return true;
}

bool callLabel(PositionState& state, const ScriptView& view, const QString& label,
               bool advanceWasCalled, PositionEffects& fx) {
    QString targetScript;
    int target = -1;
    if (!resolveCallableLabel(state, view, label, targetScript, target)) {
        return false;
    }

    const int returnLine = advanceWasCalled ? state.line : state.line + 1;
    pushFrame(state, Frame(state.script, returnLine, label, target), fx);

    if (targetScript != state.script) {
        switchToMemorySpace(state, targetScript, fx);
    }
    state.jumped = true;
    setLineInternal(state, target, /*forceEmit=*/true, fx);
    return true;
}

bool callLabelWithReturn(PositionState& state, const ScriptView& view, const QString& label,
                         int returnLine, PositionEffects& fx) {
    QString targetScript;
    int target = -1;
    if (!resolveCallableLabel(state, view, label, targetScript, target)) {
        return false;
    }

    pushFrame(state, Frame(state.script, returnLine, label, target), fx);
    if (targetScript != state.script) {
        switchToMemorySpace(state, targetScript, fx);
    }
    state.jumped = true;
    setLineInternal(state, target, /*forceEmit=*/true, fx);
    return true;
}

bool callLabelAt(PositionState& state, const ScriptView& view, const QString& script, int line,
                 const QString& returnScript, int returnLine, PositionEffects& fx) {
    const ScriptData* data = findScript(view, script);
    if (!data || line < 0 || line >= data->lines.size()) {
        return false;
    }
    pushFrame(state, Frame(returnScript, returnLine, data->lines.at(line).labelName, line), fx);
    if (script != state.script) {
        switchToMemorySpace(state, script, fx);
    }
    state.jumped = true;
    setLineInternal(state, line, /*forceEmit=*/true, fx);
    return true;
}

bool jumpLabel(PositionState& state, const ScriptView& view, const QString& label,
               PositionEffects& fx) {
    if (state.callStack.isEmpty()) {
        // 顶层（系统入口）JUMP：没有可继承的返回帧，退化为普通跳转
        return jumpToLabel(state, view, label, fx);
    }
    QString targetScript;
    int target = -1;
    if (!resolveCallableLabel(state, view, label, targetScript, target)) {
        return false;
    }

    // 新帧继承当前帧的返回地址（对齐 C# IsJump + Return 的递归语义）
    const Frame current = state.callStack.last();
    pushFrame(state, Frame(current.script, current.returnLine, label, target, /*jump=*/true), fx);
    if (targetScript != state.script) {
        switchToMemorySpace(state, targetScript, fx);
    }
    state.jumped = true;
    setLineInternal(state, target, /*forceEmit=*/true, fx);
    return true;
}

bool returnFromCall(PositionState& state, const ScriptView&, PositionEffects& fx) {
    if (state.callStack.isEmpty()) {
        return false;
    }

    const Frame frame = popFrame(state, fx);
    if (!frame.script.isEmpty() && frame.script != state.script) {
        switchToMemorySpace(state, frame.script, fx);
    }
    state.jumped = false;
    setLineInternal(state, frame.returnLine, /*forceEmit=*/true, fx);
    return true;
}

// ---------------------------------------------------------------------------
// 只读查询 / 解析
// ---------------------------------------------------------------------------

bool resolveCallableLabel(const PositionState& state, const ScriptView& view, const QString& label,
                         QString& targetScript, int& targetLine) {
    const QString upper = label.toUpper();
    const ScriptData* current = findScript(view, state.script);
    const int pos = current ? current->labelPositions.value(upper, -1) : -1;
    if (pos >= 0 && pos < current->lines.size()
        && current->lines.at(pos).kind == LineKind::FunctionLabel) {
        targetScript = state.script;
        targetLine = pos;
        return true;
    }
    if (!view.labelLists) return false;
    const auto it = view.labelLists->constFind(upper);
    if (it == view.labelLists->constEnd() || it.value().isEmpty()) return false;
    targetScript = it.value().first().script;
    targetLine = it.value().first().line;
    return true;
}

bool resolveJumpTarget(PositionState& state, const ScriptView& view, const QString& label,
                       int& position, PositionEffects& fx) {
    position = labelPosition(findScript(view, state.script), label);
    if (position >= 0) {
        return true;
    }

    if (!view.scripts) return false;
    // 迭代顺序沿用旧实现（同一张 QHash 的迭代序）：同名标签跨脚本时「命中哪个」
    // 与搬出前一致，不做额外排序。
    for (auto it = view.scripts->constBegin(); it != view.scripts->constEnd(); ++it) {
        const int pos = it.value().labelPositions.value(label.toUpper(), -1);
        if (pos >= 0) {
            position = pos;
            switchToMemorySpace(state, it.key(), fx);
            return true;
        }
    }
    return false;
}

bool resolveJumpToScript(PositionState& state, const ScriptView& view, const QString& script,
                         const QString& label, int& position, PositionEffects& fx) {
    position = labelPosition(findScript(view, script), label);
    if (position >= 0) {
        switchToMemorySpace(state, script, fx);
        return true;
    }
    return false;
}

QString executionScope(const PositionState& state, const ScriptView& view) {
    const auto* line = lineAt(view, state.script, state.line);
    return line ? line->ownerFunction : QString();
}

eravar::ScopeId executionScopeId(PositionState& state, const ScriptView& view) {
    const QString scope = executionScope(state, view);
    if (state.scopeIdValid && scope == state.scopeIdName) return state.scopeId;
    state.scopeIdName = scope;
    state.scopeId = eravar::internName(scope);
    state.scopeIdValid = true;
    return state.scopeId;
}

}  // namespace position