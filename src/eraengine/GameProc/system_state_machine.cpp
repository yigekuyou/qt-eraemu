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
#include "system_state_machine.h"

#include "script_runner.h"
#include "variable_storage.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <QElapsedTimer>

namespace {

// C# Process.AutoSaveIndex：自动存档的固定编号
constexpr int kAutoSaveIndex = 99;
// 驱动循环的安全阀（正常游戏不可能达到；用于避免处理器停滞时死循环）
constexpr int kMaxPumpIterations = 100000;

} // namespace

// ---------------------------------------------------------------------------
// 状态名 / 描述 / 权限（对齐 C# SystemStateCode 的注释）
// ---------------------------------------------------------------------------

namespace {

struct StateInfo {
    SystemStateCode code;
    const char*     name;
    const char*     description;
};

const StateInfo kStateInfos[] = {
    {SystemStateCode::Title_Begin,               "Title_Begin",               "标题画面（初始状态）"},
    {SystemStateCode::Openning,                  "Openning",                  "开场选择等待输入"},
    {SystemStateCode::Train_Begin,               "Train_Begin",               "BEGIN TRAIN 入口"},
    {SystemStateCode::Train_CallEventTrain,      "Train_CallEventTrain",      "调用 @EVENTTRAIN"},
    {SystemStateCode::Train_CallShowStatus,      "Train_CallShowStatus",      "调用 @SHOW_STATUS"},
    {SystemStateCode::Train_CallComAbleXX,       "Train_CallComAbleXX",       "调用 @COM_ABLExx"},
    {SystemStateCode::Train_CallShowUserCom,     "Train_CallShowUserCom",     "调用 @SHOW_USERCOM"},
    {SystemStateCode::Train_WaitInput,           "Train_WaitInput",           "调教指令等待输入"},
    {SystemStateCode::Train_CallEventCom,        "Train_CallEventCom",        "调用 @EVENTCOM"},
    {SystemStateCode::Train_CallComXX,           "Train_CallComXX",           "调用 @COMxx"},
    {SystemStateCode::Train_CallSourceCheck,     "Train_CallSourceCheck",     "调用 @SOURCE_CHECK"},
    {SystemStateCode::Train_CallEventComEnd,     "Train_CallEventComEnd",     "调用 @EVENTCOMEND"},
    {SystemStateCode::Train_DoTrain,             "Train_DoTrain",             "DOTRAIN 指定指令"},
    {SystemStateCode::AfterTrain_Begin,          "AfterTrain_Begin",          "BEGIN AFTERTRAIN 入口"},
    {SystemStateCode::Ablup_Begin,               "Ablup_Begin",               "BEGIN ABLUP 入口"},
    {SystemStateCode::Ablup_CallShowJuel,        "Ablup_CallShowJuel",        "调用 @SHOW_JUEL"},
    {SystemStateCode::Ablup_CallShowAblupSelect, "Ablup_CallShowAblupSelect", "调用 @SHOW_ABLUP_SELECT"},
    {SystemStateCode::Ablup_WaitInput,           "Ablup_WaitInput",           "能力上升等待输入"},
    {SystemStateCode::Ablup_CallAblupXX,         "Ablup_CallAblupXX",         "调用 @ABLUPxx / @USERABLUP"},
    {SystemStateCode::Turnend_Begin,             "Turnend_Begin",             "BEGIN TURNEND 入口"},
    {SystemStateCode::Shop_Begin,                "Shop_Begin",                "BEGIN SHOP 入口"},
    {SystemStateCode::Shop_CallEventShop,        "Shop_CallEventShop",        "调用 @EVENTSHOP"},
    {SystemStateCode::Shop_CallShowShop,         "Shop_CallShowShop",         "调用 @SHOW_SHOP"},
    {SystemStateCode::Shop_WaitInput,            "Shop_WaitInput",            "商店等待输入"},
    {SystemStateCode::Shop_CallEventBuy,         "Shop_CallEventBuy",         "调用 @USERSHOP / @EVENTBUY"},
    {SystemStateCode::SaveGame_Begin,            "SaveGame_Begin",            "SAVEGAME 入口"},
    {SystemStateCode::SaveGame_WaitInput,        "SaveGame_WaitInput",        "存档编号等待输入"},
    {SystemStateCode::SaveGame_WaitInputOverwrite, "SaveGame_WaitInputOverwrite", "覆盖确认等待输入"},
    {SystemStateCode::SaveGame_CallSaveInfo,     "SaveGame_CallSaveInfo",     "调用 @SAVEINFO"},
    {SystemStateCode::LoadGame_Begin,            "LoadGame_Begin",            "LOADGAME 入口"},
    {SystemStateCode::LoadGame_WaitInput,        "LoadGame_WaitInput",        "读档编号等待输入"},
    {SystemStateCode::LoadGameOpenning_Begin,    "LoadGameOpenning_Begin",    "开场读档入口"},
    {SystemStateCode::LoadGameOpenning_WaitInput, "LoadGameOpenning_WaitInput", "开场读档编号等待输入"},
    {SystemStateCode::AutoSave_CallSaveInfo,     "AutoSave_CallSaveInfo",     "自动存档：调用 @SAVEINFO"},
    {SystemStateCode::AutoSave_CallUniqueAutosave, "AutoSave_CallUniqueAutosave", "自动存档：调用 @SYSTEM_AUTOSAVE"},
    {SystemStateCode::AutoSave_Skipped,          "AutoSave_Skipped",          "自动存档被跳过"},
    {SystemStateCode::LoadData_DataLoaded,       "LoadData_DataLoaded",       "数据读取完成"},
    {SystemStateCode::LoadData_CallSystemLoad,   "LoadData_CallSystemLoad",   "调用 @SYSTEM_LOADEND"},
    {SystemStateCode::LoadData_CallEventLoad,    "LoadData_CallEventLoad",    "调用 @EVENTLOAD"},
    {SystemStateCode::Openning_TitleLoadgame,    "Openning_TitleLoadgame",    "标题读档（@TITLE_LOADGAME）"},
    {SystemStateCode::System_Reloaderb,          "System_Reloaderb",          "重新装载 ERB"},
    {SystemStateCode::First_Begin,               "First_Begin",               "BEGIN FIRST 入口"},
    {SystemStateCode::Normal,                    "Normal",                    "普通状态"},
};

} // namespace

QString SystemStateMachine::stateName(SystemStateCode state) {
    for (const StateInfo& info : kStateInfos) {
        if (info.code == state) return QString::fromLatin1(info.name);
    }
    return QStringLiteral("Unknown(%1)").arg(static_cast<int>(state));
}

QString SystemStateMachine::stateDescription(SystemStateCode state) {
    for (const StateInfo& info : kStateInfos) {
        if (info.code == state) return QString::fromUtf8(info.description);
    }
    return QString();
}

bool SystemStateMachine::isSavePermitted(SystemStateCode state) {
    return (static_cast<int>(state) & static_cast<int>(SystemStateCode::__CAN_SAVE__)) != 0;
}

bool SystemStateMachine::isBeginPermitted(SystemStateCode state) {
    return (static_cast<int>(state) & static_cast<int>(SystemStateCode::__CAN_BEGIN__)) != 0;
}

// ---------------------------------------------------------------------------
// 构造 / 初始化
// ---------------------------------------------------------------------------

SystemStateMachine::SystemStateMachine(ProcessState* state, EraParseTable* table, QObject* parent)
    : QObject(parent)
    , m_state(state)
    , m_table(table)
{
}

SystemStateMachine::~SystemStateMachine() = default;

void SystemStateMachine::initialize() {
    if (m_initialized) {
        return;
    }
    m_initialized = true;

    // 对齐 C# Process.initSystemProcess()：42 条状态 -> 处理函数
    m_handlers = {
        {SystemStateCode::Title_Begin,               [this] { beginTitle(); }},
        {SystemStateCode::Openning,                  [this] { endOpenning(); }},

        {SystemStateCode::Train_Begin,               [this] { beginTrain(); }},
        {SystemStateCode::Train_CallEventTrain,      [this] { endCallEventTrain(); }},
        {SystemStateCode::Train_CallShowStatus,      [this] { endCallShowStatus(); }},
        {SystemStateCode::Train_CallComAbleXX,       [this] { endCallComAbleXX(); }},
        {SystemStateCode::Train_CallShowUserCom,     [this] { endCallShowUserCom(); }},
        {SystemStateCode::Train_WaitInput,           [this] { trainWaitInput(); }},
        {SystemStateCode::Train_CallEventCom,        [this] { endEventCom(); }},
        {SystemStateCode::Train_CallComXX,           [this] { endCallComXX(); }},
        {SystemStateCode::Train_CallSourceCheck,     [this] { endCallSourceCheck(); }},
        {SystemStateCode::Train_CallEventComEnd,     [this] { endCallEventComEnd(); }},
        {SystemStateCode::Train_DoTrain,             [this] { doTrain(); }},

        {SystemStateCode::AfterTrain_Begin,          [this] { beginAfterTrain(); }},

        {SystemStateCode::Ablup_Begin,               [this] { beginAblup(); }},
        {SystemStateCode::Ablup_CallShowJuel,        [this] { endCallShowJuel(); }},
        {SystemStateCode::Ablup_CallShowAblupSelect, [this] { endCallShowAblupSelect(); }},
        {SystemStateCode::Ablup_WaitInput,           [this] { ablupWaitInput(); }},
        {SystemStateCode::Ablup_CallAblupXX,         [this] { endCallAblupXX(); }},

        {SystemStateCode::Turnend_Begin,             [this] { beginTurnend(); }},

        {SystemStateCode::Shop_Begin,                [this] { beginShop(); }},
        {SystemStateCode::Shop_CallEventShop,        [this] { endCallEventShop(); }},
        {SystemStateCode::Shop_CallShowShop,         [this] { endCallShowShop(); }},
        {SystemStateCode::Shop_WaitInput,            [this] { shopWaitInput(); }},
        {SystemStateCode::Shop_CallEventBuy,         [this] { endCallEventBuy(); }},

        {SystemStateCode::SaveGame_Begin,            [this] { beginSaveGame(); }},
        {SystemStateCode::SaveGame_WaitInput,        [this] { saveGameWaitInput(); }},
        {SystemStateCode::SaveGame_WaitInputOverwrite, [this] { saveGameWaitInputOverwrite(); }},
        {SystemStateCode::SaveGame_CallSaveInfo,     [this] { endCallSaveInfo(); }},
        {SystemStateCode::LoadGame_Begin,            [this] { beginLoadGame(); }},
        {SystemStateCode::LoadGame_WaitInput,        [this] { loadGameWaitInput(); }},
        {SystemStateCode::LoadGameOpenning_Begin,    [this] { beginLoadGameOpening(); }},
        {SystemStateCode::LoadGameOpenning_WaitInput, [this] { loadGameWaitInput(); }},

        {SystemStateCode::AutoSave_CallSaveInfo,     [this] { endAutoSaveCallSaveInfo(); }},
        {SystemStateCode::AutoSave_CallUniqueAutosave, [this] { endAutoSave(); }},

        {SystemStateCode::LoadData_DataLoaded,       [this] { beginDataLoaded(); }},
        {SystemStateCode::LoadData_CallSystemLoad,   [this] { endSystemLoad(); }},
        {SystemStateCode::LoadData_CallEventLoad,    [this] { endEventLoad(); }},

        {SystemStateCode::Openning_TitleLoadgame,    [this] { endTitleLoadgame(); }},

        {SystemStateCode::System_Reloaderb,          [this] { endReloaderb(); }},
        {SystemStateCode::First_Begin,               [this] { beginFirst(); }},

        {SystemStateCode::Normal,                    [this] { endNormal(); }},
    };

    reloadTrainNames();
}

void SystemStateMachine::reloadTrainNames() {
    m_trainNames = m_host.trainNames ? m_host.trainNames() : QStringList();
    m_comAble = QList<int>(m_trainNames.size(), -1);
}

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------

bool SystemStateMachine::boolConfig(const std::function<bool()>& fn, bool fallback) const {
    return fn ? fn() : fallback;
}

int SystemStateMachine::intConfig(const std::function<int()>& fn, int fallback) const {
    return fn ? fn() : fallback;
}

void SystemStateMachine::printLine(const QString& text) {
    if (m_host.printSingleLine) m_host.printSingleLine(text);
}

void SystemStateMachine::printCn(const QString& text, bool flush) {
    if (m_host.printC) m_host.printC(text, flush);
    else if (m_host.print) m_host.print(text);
}

void SystemStateMachine::printTemporary(const QString& text) {
    if (m_host.printTemporaryLine) m_host.printTemporaryLine(text);
    else printLine(text);
}

void SystemStateMachine::printErrorLine(const QString& text) {
    if (m_host.printError) m_host.printError(text);
}

void SystemStateMachine::deleteLines(int count) {
    if (m_host.deleteLine) m_host.deleteLine(count);
}

void SystemStateMachine::refresh() {
    if (m_host.refreshStrings) m_host.refreshStrings();
}

void SystemStateMachine::flushOut() {
    if (m_host.printFlush) m_host.printFlush();
}

int SystemStateMachine::saveDataNos() const {
    const int n = m_host.saveDataNos ? m_host.saveDataNos() : 20;
    return n > 0 ? n : 20;
}

QString SystemStateMachine::startDateText() const {
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy/MM/dd HH:mm:ss")) + QLatin1Char(' ');
}

QString SystemStateMachine::comString(int trainCode, int comNo) const {
    const QString name = (trainCode >= 0 && trainCode < m_trainNames.size())
                             ? m_trainNames.at(trainCode) : QString();
    return QStringLiteral("%1[%2]").arg(name).arg(comNo, 3);
}

void SystemStateMachine::setState(SystemStateCode state) {
    const SystemStateCode old = m_state->getSystemState();
    if (old == state) return;
    qDebug() << "[state]" << stateName(old) << "->" << stateName(state);
    m_state->setSystemState(state);
    emit stateChanged(old, state);
}

void SystemStateMachine::setWaitInput() {
    m_atFloor = true;
    if (m_host.waitInput) m_host.waitInput();
    emit inputRequested(m_state->getSystemState());
    m_state->requestWaitSystemInput();
}

void SystemStateMachine::setWait() {
    m_atFloor = true;
    if (m_host.readAnyKey) m_host.readAnyKey();
    emit inputRequested(m_state->getSystemState());
    m_state->requestWaitSystemInput();
}

// ---------------------------------------------------------------------------
// 事件调用（C# CalledFunction 的 4 组导航）
// ---------------------------------------------------------------------------

void SystemStateMachine::eventShiftNext() {
    while (true) {
        m_event.counter++;
        if (m_event.group >= 0 && m_event.group < m_event.groups.size()
            && m_event.counter < m_event.groups.at(m_event.group).size()) {
            m_event.current = m_event.groups.at(m_event.group).at(m_event.counter);
            m_event.hasCurrent = true;
            return;
        }
        m_event.group++;
        m_event.counter = -1;
        if (m_event.group >= 4) {
            m_event.hasCurrent = false;
            return;
        }
    }
}

void SystemStateMachine::eventShiftNextGroup() {
    m_event.counter = -1;
    m_event.group++;
    if (m_event.group >= 4) {
        m_event.hasCurrent = false;
        return;
    }
    eventShiftNext();
}

void SystemStateMachine::refreshEventCurrentFlags() {
    m_event.isOnly = m_event.hasCurrent && m_event.current.isOnly;
    m_event.hasSingleFlag = m_event.hasCurrent && m_event.current.isSingle;
}

void SystemStateMachine::clearEventCall() {
    m_event = EventCall();
}

bool SystemStateMachine::advanceEventCall() {
    if (!m_event.active) {
        return false;
    }
    // 当前事件函数已返回：按 C# Process.Return 的分组规则推进
    const qint64 ret = m_storage ? m_storage->getResult(0) : 0;
    if (m_event.isOnly) {
        clearEventCall();
    } else if (m_event.hasSingleFlag && ret == 1) {
        eventShiftNextGroup();
    } else {
        eventShiftNext();
    }

    if (m_event.active && m_event.hasCurrent) {
        if (callLabelRef(m_event.current)) {
            refreshEventCurrentFlags();
            return true;
        }
    }
    clearEventCall();
    return false;
}

// ---------------------------------------------------------------------------
// 函数调用（对齐 C# Process.callFunction / CalledFunction.CallFunction）
// ---------------------------------------------------------------------------

bool SystemStateMachine::callFunction(const QString& name, bool force, bool isEvent) {
    if (!m_table) {
        return false;
    }
    const QList<LabelRef> refs = m_table->labels(name);
    qDebug() << "[exec] callFunction @" << name << "force =" << force << "isEvent =" << isEvent;

    if (isEvent) {
        QList<LabelRef> events;
        for (const LabelRef& r : refs) {
            if (r.isEvent) events.append(r);
        }
        if (events.isEmpty()) {
            if (force) {
                emit errorOccurred(QStringLiteral("找不到函数\"@%1\"").arg(name));
            }
            return false;
        }
        EventCall ec;
        ec.active = true;
        ec.name = name;
        ec.groups = QList<QList<LabelRef>>(4);
        for (const LabelRef& r : events) {
            if (r.isOnly) ec.groups[0].append(r);
            if (r.isPri) ec.groups[1].append(r);
            if (!r.isPri && !r.isLater) ec.groups[2].append(r);
            if (r.isLater) ec.groups[3].append(r);
        }
        ec.group = -1;
        ec.counter = -1;
        m_event = ec;
        eventShiftNext();
        if (!m_event.hasCurrent) {
            clearEventCall();
            return false;
        }
        refreshEventCurrentFlags();
        if (!callLabelRef(m_event.current)) {
            clearEventCall();
            return false;
        }
        return true;
    }

    // 非事件：优先取非事件声明（C# GetNonEventLabel）
    LabelRef chosen;
    bool found = false;
    bool onlyEvent = false;
    for (const LabelRef& r : refs) {
        if (!r.isEvent) { chosen = r; found = true; break; }
        onlyEvent = true;
    }
    if (!found) {
        if (onlyEvent && !boolConfig(m_host.compatiCallEvent, false)) {
            emit errorOccurred(QStringLiteral("事件函数@%1不能作为普通函数调用").arg(name));
            return false;
        }
        if (force) {
            emit errorOccurred(QStringLiteral("找不到函数\"@%1\"").arg(name));
        }
        return false;
    }
    return callLabelRef(chosen);
}

// 系统层调用脚本函数：返回脚本 = 被调脚本自身，返回地址 = 该脚本末尾，
// 于是函数体结束（顺落下一个函数标签）时正好回到「系统层」。
bool SystemStateMachine::callLabelRef(const LabelRef& ref) {
    if (!m_table || ref.line < 0 || ref.script.isEmpty()) {
        return false;
    }
    // 函数私有变量初值（#DIM X = 7）
    m_table->applyPrivateVariableDefaults(m_table->labelNameAt(ref.script, ref.line));
    const int retLine = m_table->scriptLineCount(ref.script);
    return m_table->callLabelAt(ref.script, ref.line, ref.script, retLine);
}

// ---------------------------------------------------------------------------
// BEGIN / 指令钩子
// ---------------------------------------------------------------------------

bool SystemStateMachine::beginWithKeyword(const QString& keyword, QString* error) {
    // 错误消息里带上发起 BEGIN 的函数名（对齐 C# functionList[0].FunctionName）
    const QString funcName = m_table ? m_table->currentFrame().callLabel : QString();
    return m_state->setBeginKeyword(keyword, error, funcName);
}

bool SystemStateMachine::beginWithType(BeginType type, QString* error) {
    const QString funcName = m_table ? m_table->currentFrame().callLabel : QString();
    return m_state->processBegin(type, error, funcName);
}

void SystemStateMachine::setCommands(qint64 count) {
    m_coms.clear();
    m_isCTrain = true;
    for (int i = 0; i < count; ++i) {
        m_coms.append(m_storage ? static_cast<int>(m_storage->getSelectcom(i + 1)) : 0);
    }
}

bool SystemStateMachine::clearCommands() {
    m_coms.clear();
    m_count = 0;
    m_isCTrain = false;
    m_skipPrint = true;
    return callFunction(QStringLiteral("CALLTRAINEND"), false, false);
}

void SystemStateMachine::abortCallTrain() {
    // 对齐 C# DOTRAIN_Instruction：coms.Clear(); isCTrain = false; count = 0;
    // 目的：CALLTRAIN 处理途中执行 DOTRAIN 时，CALLTRAIN 的剩余部分作废
    // （文档 Command.html「DOTRAIN」条目）。**不**调用 @CALLTRAINEND。
    m_coms.clear();
    m_count = 0;
    m_isCTrain = false;
}

void SystemStateMachine::requestSaveLoad(bool save) {
    qDebug() << "[state] requestSaveLoad" << (save ? "SAVE" : "LOAD");
    m_prevStates.append(m_state->getSystemState());
    setState(save ? SystemStateCode::SaveGame_Begin : SystemStateCode::LoadGame_Begin);
}

void SystemStateMachine::loadPrevState() {
    if (m_prevStates.isEmpty()) {
        beginTitle();
        return;
    }
    const SystemStateCode prev = m_prevStates.takeLast();
    setState(prev);
    if (prev == SystemStateCode::Normal) {
        return;
    }
    runSystemProc();
}

void SystemStateMachine::deletePrevState() {
    if (!m_prevStates.isEmpty()) {
        m_prevStates.removeLast();
    }
}

// ---------------------------------------------------------------------------
// 驱动循环（C# Process.DoScript 的门控循环）
// ---------------------------------------------------------------------------

ExecState SystemStateMachine::run() {
    ++m_waitGeneration;
    ++m_runGeneration;
    m_pumpScheduled = false;
    initialize();
    // C# DoScript：从**系统层**起步（Title_Begin -> beginTitle -> @SYSTEM_TITLE）。
    // 清掉装载期可能残留的位置，避免把首个函数标签当入口执行。
    if (m_table) {
        m_table->resetPosition();
    }
    m_atFloor = true;
    return pump();
}

ExecState SystemStateMachine::resume(qint64 value) {
    // 出错后不再继续驱动（避免反复执行同一条出错指令）
    if (m_state->getExecState() == ExecState::Error) {
        return ExecState::Error;
    }
    ++m_waitGeneration;
    m_systemResult = value;
    if (m_storage) {
        m_storage->setResult(0, value);
        m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, value);
    }
    m_state->setExecState(ExecState::Continue);
    return pump();
}

ExecState SystemStateMachine::resumeString(const QString& value) {
    if (m_storage) {
        // [qdbug] 修复：RESULTS 全局（C# VariableData.cs:202，跨函数共享；
        //   INPUTS 写入的 RESULTS 在任何函数里都应读到）
        m_storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0, value);
    }
    return resume(0);
}

ExecState SystemStateMachine::deliverInputValues(const QList<qint64>& values) {
    ++m_waitGeneration;
    if (m_state->getExecState() == ExecState::Error) {
        return ExecState::Error;
    }
    if (m_storage) {
        for (int i = 0; i < values.size(); ++i) {
            m_storage->setSystemVariable(QStringLiteral("RESULT"), i, values.at(i));
            m_storage->setResult(i, values.at(i));
        }
    }
    m_systemResult = values.isEmpty() ? 0 : values.first();
    m_state->setExecState(ExecState::Continue);
    return pump();
}

// ---------------------------------------------------------------------------
// 实时/限时输入（AWAIT / INPUTMOUSEKEY / TONEINPUT）
//
// 这三者都是**脚本层**的挂起（挂起时脚本停在当前行，恢复后继续跑脚本），
// 因此不动 m_atFloor（区别于系统层的 setWaitInput）。
// ---------------------------------------------------------------------------

void SystemStateMachine::awaitDelay(int ms) {
    if (ms <= 0) {
        return;   // 不挂起，继续执行
    }
    m_state->requestWaitSystemInput();   // 只挂起，不请求输入 UI（AWAIT 不是输入）
    if (!m_timer) {
        return;   // 未注入计时器：由外部负责恢复（引擎侧始终注入 QTimer）
    }
    const auto wait = ++m_waitGeneration;
    m_timer(ms, [this, wait]() { if (wait == m_waitGeneration) resume(0); });
}

void SystemStateMachine::waitMouseKey(int timeoutMs) {
    const auto wait = ++m_waitGeneration;
    m_state->setExecState(ExecState::WaitInput);
    emit inputRequested(m_state->getSystemState());
    if (timeoutMs > 0 && m_timer) {
        m_timer(timeoutMs, [this, wait]() {
            if (wait != m_waitGeneration) return;
            // C#：INPUTMOUSEKEY 超时 -> InputMouseKey(4,0,0,0,0)
            deliverInputValues({4, 0, 0, 0, 0});
        });
    }
}

void SystemStateMachine::waitAnyKey() {
    // C# Console.ReadAnyKey()：挂起等待「任意键」（Enter/点击），无超时
    m_state->setExecState(ExecState::WaitInput);
    emit inputRequested(m_state->getSystemState());
    if (m_host.readAnyKey) m_host.readAnyKey();   // 通知 UI 弹「任意键」等待
}

void SystemStateMachine::waitTimedInput(int timeoutMs, qint64 defaultValue) {
    const auto wait = ++m_waitGeneration;
    m_state->setExecState(ExecState::WaitInput);
    emit inputRequested(m_state->getSystemState());
    if (timeoutMs > 0 && m_timer) {
        // [qdbug] C# 原版全量：TINPUT 超时交付**缺省值**（此前恒为 0）
        m_timer(timeoutMs, [this, wait, defaultValue]() {
            if (wait == m_waitGeneration) deliverInputValues({defaultValue});
        });
    }
}

// TINPUTS：限时字符串输入；超时没输入则 RESULTS = 缺省字符串并继续
//（RESULTS 全局，C# VariableData.cs:202）
void SystemStateMachine::waitTimedStringInput(int timeoutMs, const QString& defaultValue) {
    const auto wait = ++m_waitGeneration;
    m_state->setExecState(ExecState::WaitInput);
    emit inputRequested(m_state->getSystemState());
    if (timeoutMs > 0 && m_timer) {
        m_timer(timeoutMs, [this, wait, defaultValue]() {
            if (wait != m_waitGeneration) return;
            m_storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0, defaultValue);
            resume(0);
        });
    }
}

void SystemStateMachine::schedulePump() {
    if (m_pumpScheduled) return;
    m_pumpScheduled = true;
    const auto generation = m_runGeneration;
    QTimer::singleShot(0, Qt::PreciseTimer, this, [this, generation] {
        if (generation != m_runGeneration) return;
        m_pumpScheduled = false;
        if (m_state->isRunning()) pump();
    });
}

ExecState SystemStateMachine::pump() {
    initialize();
    if (m_pumpActive) {
        return m_state->getExecState();
    }
    m_pumpActive = true;

    int guard = 0;
    QElapsedTimer sliceTime;
    sliceTime.start();
    for (;;) {
        if (!m_state->isRunning()) {
            break;
        }
        if (++guard > kMaxPumpIterations) {
            emit errorOccurred(QStringLiteral("系统状态机似乎进入了无限循环"));
            m_state->setErrorState();
            break;
        }

        if (!m_atFloor) {
            // ---- 脚本层：跑到挂起或回到帧底 ----
            if (!m_runner) {
                m_atFloor = true;
            } else {
                if (m_pacingEnabled) m_runner->runSlice(4096, 3);
                else m_runner->runToCompletion();
                if (m_pacingEnabled && m_state->isRunning()) {
                    schedulePump();
                    break;
                }
                // QUIT：彻底结束（不能被当成「脚本回到底层」而重启标题）
                if (m_state->quitRequested()) return m_state->getExecState();
                const ExecState st = m_state->getExecState();
                if (st == ExecState::WaitInput || st == ExecState::WaitSystemInput
                    || st == ExecState::Error) {
                    break;
                }
                // Halt = 脚本函数返回 / 脚本执行到帧底 → 回到系统层继续驱动
                m_atFloor = true;
                m_state->setExecState(ExecState::Continue);
            }
        }

        if (m_state->quitRequested()) return m_state->getExecState();
        if (!m_state->isRunning()) {
            break;
        }

        if (m_pacingEnabled && (guard > 64 || sliceTime.elapsed() >= 4)) {
            schedulePump();
            break;
        }

        // ---- 系统层 ----
        // 1) BEGIN 已请求：切换状态（C# Return() 里 functionList 见底时调用 Begin()）
        if (m_state->getBeginType() != BeginType::NONE) {
            m_state->beginFromType();
        }

        // 2) 事件函数返回后：若还有同名函数就继续跑，不进入状态处理器
        if (advanceEventCall()) {
            m_atFloor = false;
            continue;
        }

        // 3) 运行当前状态的处理函数
        const SystemStateCode before = m_state->getSystemState();
        const int depthBefore = m_table ? m_table->depth() : 0;
        runSystemProc();
        emit stateAdvanced(m_state->getSystemState());

        if (!m_state->isRunning()) {
            break;   // 处理函数请求了系统输入
        }
        if (m_table && m_table->depth() > depthBefore) {
            m_atFloor = false;   // 处理函数调用了脚本函数
            continue;
        }
        if (m_state->getSystemState() != before) {
            continue;   // 状态推进：继续处理下一个状态
        }
        // 既没调用脚本、也没切换状态、也没挂起 —— 视为停滞（C# 会死循环）
        emit errorOccurred(
            QStringLiteral("系统状态 %1 没有产生任何进展").arg(stateName(before)));
        m_state->setErrorState();
        break;
    }

    m_pumpActive = false;
    return m_state->getExecState();
}

void SystemStateMachine::runSystemProc() {
    initialize();
    const SystemStateCode state = m_state->getSystemState();
    const auto it = m_handlers.constFind(state);
    if (it == m_handlers.constEnd()) {
        emit errorOccurred(QStringLiteral("未定义的系统状态 %1").arg(stateName(state)));
        m_state->setErrorState();
        return;
    }
    it.value()();
}

// ---------------------------------------------------------------------------
// 状态处理器（逐条对齐 C# Process.SystemProc.cs）
// ---------------------------------------------------------------------------

void SystemStateMachine::beginTitle() {
    // 连续调教命令处理中的状态若被带到这里，先清掉
    if (m_isCTrain && clearCommands()) {
        return;
    }
    m_skipPrint = false;

    // @SYSTEM_TITLE 存在则用自定义标题画面（对齐 C# callFunction("SYSTEM_TITLE", false, false)）
    if (callFunction(QStringLiteral("SYSTEM_TITLE"), false, false)) {
        setState(SystemStateCode::Normal);
        return;
    }

    // 标准标题画面
    if (m_host.printBar) m_host.printBar();
    if (m_host.newLine) m_host.newLine();
    if (m_host.setAlignment) m_host.setAlignment(1);   // Center
    printLine(m_host.scriptTitle ? m_host.scriptTitle() : QString());
    if ((m_host.scriptVersion ? m_host.scriptVersion() : 0) != 0) {
        printLine(m_host.scriptVersionText ? m_host.scriptVersionText() : QString());
    }
    printLine(m_host.scriptAutherName ? m_host.scriptAutherName() : QString());
    printLine(QStringLiteral("(%1)").arg(m_host.scriptYear ? m_host.scriptYear() : QString()));
    if (m_host.newLine) m_host.newLine();
    printLine(m_host.scriptDetail ? m_host.scriptDetail() : QString());
    if (m_host.setAlignment) m_host.setAlignment(0);   // Left

    if (m_host.printBar) m_host.printBar();
    if (m_host.newLine) m_host.newLine();
    printLine(QStringLiteral("[0] ") + (m_host.titleMenuString ? m_host.titleMenuString(0) : QString()));
    printLine(QStringLiteral("[1] ") + (m_host.titleMenuString ? m_host.titleMenuString(1) : QString()));
    openingInput();
}

void SystemStateMachine::openingInput() {
    setWaitInput();
    setState(SystemStateCode::Openning);
}

void SystemStateMachine::endOpenning() {
    if (m_systemResult == 0) {
        // [0] 从头开始
        if (m_host.resetData) m_host.resetData();
        if (m_host.addCharacterFromCsvNo) m_host.addCharacterFromCsvNo(0);
        const int def = m_host.defaultCharacter ? m_host.defaultCharacter() : 0;
        if (def > 0 && m_host.addCharacterFromCsvNo) m_host.addCharacterFromCsvNo(def);
        if (m_host.printBar) m_host.printBar();
        if (m_host.newLine) m_host.newLine();
        beginFirst();
    } else if (m_systemResult == 1) {
        if (callFunction(QStringLiteral("TITLE_LOADGAME"), false, false)) {
            setState(SystemStateCode::Openning_TitleLoadgame);
        } else {
            beginLoadGameOpening();
        }
    } else {
        // 输入非法：重画选项，要求重新选择
        deleteLines(1);
        printTemporary(QStringLiteral("无效的值"));
        openingInput();
    }
}

void SystemStateMachine::beginFirst() {
    setState(SystemStateCode::Normal);
    if (m_isCTrain && clearCommands()) {
        return;
    }
    m_skipPrint = false;
    callFunction(QStringLiteral("EVENTFIRST"), true, true);
}

void SystemStateMachine::endTitleLoadgame() {
    beginTitle();
}

void SystemStateMachine::beginTrain() {
    // C# vEvaluator.UpdateInBeginTrain()：变量层的指令计数复位，本移植尚未接入
    setState(SystemStateCode::Train_CallEventTrain);
    if (!callFunction(QStringLiteral("EVENTTRAIN"), false, true)) {
        endCallEventTrain();
    }
}

void SystemStateMachine::endCallEventTrain() {
    const qint64 next = m_storage ? m_storage->getNextcom(0) : 0;
    if (next >= 0) {
        setState(SystemStateCode::Train_CallEventCom);
        if (m_storage) {
            m_storage->setSelectcom(0, next);
            m_storage->setNextcom(0, 0);
        }
        callEventCom();
        return;
    }

    if (m_isCTrain) {
        m_skipPrint = true;
    }
    callFunction(QStringLiteral("SHOW_STATUS"), true, false);
    setState(SystemStateCode::Train_CallShowStatus);
}

void SystemStateMachine::endCallShowStatus() {
    setState(SystemStateCode::Train_CallComAbleXX);
    m_lastCalledComable = -1;
    m_lastAddCom = -1;
    m_printComCount = 0;
    for (int& v : m_comAble) v = -1;
    endCallComAbleXX();
}

void SystemStateMachine::endCallComAbleXX() {
    // 上一轮 @COM_ABLExx 返回：RESULT != 0 才追加为可选项
    if (m_lastCalledComable >= 0 && m_lastCalledComable < m_trainNames.size()
        && !m_trainNames.at(m_lastCalledComable).isEmpty()) {
        m_lastAddCom++;
        const qint64 result = m_storage ? m_storage->getResult(0) : 0;
        if (result != 0) {
            if (m_lastAddCom >= 0 && m_lastAddCom < m_comAble.size()) {
                m_comAble[m_lastAddCom] = m_lastCalledComable;
            }
            if (!m_isCTrain) {
                printCn(comString(m_lastCalledComable, m_lastAddCom), true);
                m_printComCount++;
                const int per = intConfig(m_host.printCPerLine, 0);
                if (per > 0 && m_printComCount % per == 0) flushOut();
            }
            refresh();
        }
    }

    // 继续查找下一个有定义的 @COM_ABLExx
    while (++m_lastCalledComable < m_trainNames.size()) {
        if (m_trainNames.at(m_lastCalledComable).isEmpty()) {
            continue;
        }
        const QString comName = QStringLiteral("COM_ABLE%1").arg(m_lastCalledComable);
        if (!callFunction(comName, false, false)) {
            m_lastAddCom++;
            if (intConfig(m_host.comAbleDefault, 0) == 0) {
                continue;
            }
            if (m_lastAddCom >= 0 && m_lastAddCom < m_comAble.size()) {
                m_comAble[m_lastAddCom] = m_lastCalledComable;
            }
            if (!m_isCTrain) {
                printCn(comString(m_lastCalledComable, m_lastAddCom), true);
                m_printComCount++;
                const int per = intConfig(m_host.printCPerLine, 0);
                if (per > 0 && m_printComCount % per == 0) flushOut();
            }
            continue;
        }
        refresh();
        return;
    }

    if (m_lastCalledComable >= m_trainNames.size()) {
        setState(SystemStateCode::Train_CallShowUserCom);
        flushOut();
        refresh();
        callFunction(QStringLiteral("SHOW_USERCOM"), true, false);
    }
}

void SystemStateMachine::endCallShowUserCom() {
    if (m_skipPrint) {
        m_skipPrint = false;
    }
    // C# vEvaluator.UpdateAfterShowUsercom()：UP/DOWN/LOSEBASE + 角色 DOWNBASE/CUP/CDOWN 归零
    if (m_storage) m_storage->updateAfterShowUsercom();
    if (!m_isCTrain) {
        setWaitInput();
        setState(SystemStateCode::Train_WaitInput);
    } else if (m_count < m_coms.size()) {
        m_systemResult = m_coms.at(m_count);
        m_count++;
        trainWaitInput();
    }
}

void SystemStateMachine::trainWaitInput() {
    int selectCom = -1;
    if (!m_isCTrain) {
        if (m_systemResult >= 0 && m_systemResult < m_comAble.size()) {
            selectCom = m_comAble.at(m_systemResult);
        }
    } else {
        for (int i = 0; i < m_comAble.size(); ++i) {
            if (m_comAble.at(i) == m_systemResult) {
                selectCom = static_cast<int>(m_systemResult);
            }
        }
        printLine(QStringLiteral("<连续执行指令：%1/%2>").arg(m_count).arg(m_coms.size()));
    }

    if (selectCom >= 0) {
        if (m_storage) m_storage->setSelectcom(0, selectCom);
        callEventCom();
    } else {
        if (m_isCTrain) {
            printLine(QStringLiteral("无法执行指令"));
        }
        if (m_storage) {
            m_storage->setResult(0, m_systemResult);
            m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, m_systemResult);
        }
        setState(SystemStateCode::Train_CallEventComEnd);
        callFunction(QStringLiteral("USERCOM"), true, false);
    }
}

void SystemStateMachine::callEventCom() {
    // C# vEvaluator.UpdateAfterInputCom()：各角色 NOWEX 归零（选定指令即将执行前）
    if (m_storage) m_storage->updateAfterInputCom();
    setState(SystemStateCode::Train_CallEventCom);
    if (!callFunction(QStringLiteral("EVENTCOM"), false, true)) {
        endEventCom();
    }
}

void SystemStateMachine::doTrain() {
    // C# Process.SystemProc.cs doTrain()：
    //   UpdateAfterShowUsercom(); SELECTCOM = doTrainSelectCom; callEventCom();
    if (m_storage) m_storage->updateAfterShowUsercom();
    if (m_storage) m_storage->setSelectcom(0, m_doTrainSelectCom);
    callEventCom();
}

void SystemStateMachine::endEventCom() {
    const qint64 selectCom = m_storage ? m_storage->getSelectcom(0) : 0;
    const QString comName = QStringLiteral("COM%1").arg(selectCom);
    setState(SystemStateCode::Train_CallComXX);
    callFunction(comName, true, false);
}

void SystemStateMachine::endCallComXX() {
    const qint64 result = m_storage ? m_storage->getResult(0) : 0;
    if (result == 0) {
        endCallEventComEnd();
    } else {
        setState(SystemStateCode::Train_CallSourceCheck);
        callFunction(QStringLiteral("SOURCE_CHECK"), true, false);
    }
}

void SystemStateMachine::endCallSourceCheck() {
    setState(SystemStateCode::Train_CallEventComEnd);
    m_needWaitToEventComEnd = true;
    if (!callFunction(QStringLiteral("EVENTCOMEND"), false, true)) {
        endCallEventComEnd();
    }
}

void SystemStateMachine::endCallEventComEnd() {
    if (boolConfig(m_host.lastLineIsTemporary, false) && !m_isCTrain && m_needCheck) {
        if (boolConfig(m_host.lastLineIsEmpty, false)) {
            deleteLines(2);
            printTemporary(QStringLiteral("无效的值"));
        }
        endCallShowUserCom();
        return;
    }

    if (m_isCTrain && m_count == m_coms.size()) {
        m_isCTrain = false;
        m_skipPrint = false;
        m_coms.clear();
        m_count = 0;
        if (callFunction(QStringLiteral("CALLTRAINEND"), false, false)) {
            m_needCheck = false;
            return;
        }
    }
    m_needCheck = true;
    if (m_needWaitToEventComEnd) {
        setWait();
    }
    m_needWaitToEventComEnd = false;
    endCallEventTrain();
}

void SystemStateMachine::beginAfterTrain() {
    if (m_isCTrain && clearCommands()) {
        return;
    }
    m_skipPrint = false;
    setState(SystemStateCode::Normal);
    callFunction(QStringLiteral("EVENTEND"), true, true);
}

void SystemStateMachine::beginAblup() {
    if (m_isCTrain && clearCommands()) {
        return;
    }
    m_skipPrint = false;
    setState(SystemStateCode::Ablup_CallShowJuel);
    callFunction(QStringLiteral("SHOW_JUEL"), true, false);
}

void SystemStateMachine::endCallShowJuel() {
    setState(SystemStateCode::Ablup_CallShowAblupSelect);
    callFunction(QStringLiteral("SHOW_ABLUP_SELECT"), true, false);
}

void SystemStateMachine::endCallShowAblupSelect() {
    setWaitInput();
    setState(SystemStateCode::Ablup_WaitInput);
}

void SystemStateMachine::ablupWaitInput() {
    // 未定义 @ABLUPxx 也允许 ABLUP（否则 [99] 反发刻印之类做不了）
    if (m_systemResult >= 0 && m_systemResult < 100) {
        setState(SystemStateCode::Ablup_CallAblupXX);
        const QString ablName = QStringLiteral("ABLUP%1").arg(m_systemResult);
        if (!callFunction(ablName, false, false)) {
            deleteLines(1);
            printTemporary(QStringLiteral("无效的值"));
            endCallShowAblupSelect();
        }
    } else {
        if (m_storage) {
            m_storage->setResult(0, m_systemResult);
            m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, m_systemResult);
        }
        setState(SystemStateCode::Ablup_CallAblupXX);
        callFunction(QStringLiteral("USERABLUP"), true, false);
    }
}

void SystemStateMachine::endCallAblupXX() {
    if (boolConfig(m_host.lastLineIsTemporary, false)) {
        if (boolConfig(m_host.lastLineIsEmpty, false)) {
            deleteLines(2);
            printTemporary(QStringLiteral("无效的值"));
        }
        endCallShowAblupSelect();
    } else {
        beginAblup();
    }
}

void SystemStateMachine::beginTurnend() {
    if (m_isCTrain && clearCommands()) {
        return;
    }
    m_skipPrint = false;
    callFunction(QStringLiteral("EVENTTURNEND"), true, true);
    setState(SystemStateCode::Normal);
}

void SystemStateMachine::beginShop() {
    if (m_isCTrain && clearCommands()) {
        return;
    }
    m_skipPrint = false;
    setState(SystemStateCode::Shop_CallEventShop);
    if (!callFunction(QStringLiteral("EVENTSHOP"), false, true)) {
        endCallEventShop();
    }
}

void SystemStateMachine::endCallEventShop() {
    m_saveTarget = -1;
    if (boolConfig(m_host.autoSave, false) && m_state->calledWhenNormal()) {
        beginAutoSave();
    } else {
        setState(SystemStateCode::AutoSave_Skipped);
        endAutoSaveCallSaveInfo();
    }
}

void SystemStateMachine::beginAutoSave() {
    if (callFunction(QStringLiteral("SYSTEM_AUTOSAVE"), false, false)) {
        setState(SystemStateCode::AutoSave_CallUniqueAutosave);
        return;
    }
    m_saveTarget = kAutoSaveIndex;
    setState(SystemStateCode::AutoSave_CallSaveInfo);
    if (!callFunction(QStringLiteral("SAVEINFO"), false, false)) {
        endAutoSaveCallSaveInfo();
    }
}

void SystemStateMachine::endAutoSaveCallSaveInfo() {
    if (m_saveTarget == kAutoSaveIndex) {
        const QString text = startDateText();
        if (!(m_host.saveTo && m_host.saveTo(m_saveTarget, text))) {
            printErrorLine(QStringLiteral("自动存档时发生意外错误"));
            printErrorLine(QStringLiteral("跳过自动存档"));
            if (m_host.readAnyKey) m_host.readAnyKey();
        }
    }
    endAutoSave();
}

void SystemStateMachine::endAutoSave() {
    if (m_state->isBegun()) {
        m_state->beginFromType();
        return;
    }
    setState(SystemStateCode::Shop_CallShowShop);
    callFunction(QStringLiteral("SHOW_SHOP"), true, false);
}

void SystemStateMachine::endCallShowShop() {
    setWaitInput();
    setState(SystemStateCode::Shop_WaitInput);
}

void SystemStateMachine::shopWaitInput() {
    const int maxItem = intConfig(m_host.maxShopItem, 0);
    if (m_systemResult >= 0 && m_systemResult < maxItem) {
        if (m_host.itemSales && m_host.itemSales(static_cast<int>(m_systemResult))) {
            if (m_host.buyItem && m_host.buyItem(static_cast<int>(m_systemResult))) {
                setState(SystemStateCode::Shop_CallEventBuy);
                if (!callFunction(QStringLiteral("EVENTBUY"), false, true)) {
                    endCallEventBuy();
                }
                return;
            }
            deleteLines(1);
            printTemporary(QStringLiteral("钱不够。"));
        } else {
            deleteLines(1);
            printTemporary(QStringLiteral("没有卖。"));
        }
        endCallShowShop();
        return;
    }
    if (m_storage) {
        m_storage->setResult(0, m_systemResult);
        m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, m_systemResult);
    }
    callFunction(QStringLiteral("USERSHOP"), true, false);
    setState(SystemStateCode::Shop_CallEventBuy);
}

void SystemStateMachine::endCallEventBuy() {
    if (boolConfig(m_host.lastLineIsTemporary, false)) {
        if (boolConfig(m_host.lastLineIsEmpty, false)) {
            deleteLines(2);
            printTemporary(QStringLiteral("无效的值"));
        }
        endCallShowShop();
    } else {
        endAutoSave();
    }
}

void SystemStateMachine::beginDataLoaded() {
    setState(SystemStateCode::LoadData_CallSystemLoad);
    if (!callFunction(QStringLiteral("SYSTEM_LOADEND"), false, false)) {
        endSystemLoad();
    }
}

void SystemStateMachine::endSystemLoad() {
    setState(SystemStateCode::LoadData_CallEventLoad);
    if (!callFunction(QStringLiteral("EVENTLOAD"), false, true)) {
        endAutoSave();
    }
}

void SystemStateMachine::endEventLoad() {
    endAutoSave();
}

void SystemStateMachine::beginSaveGame() {
    printLine(QStringLiteral("要保存到第几号？"));
    setState(SystemStateCode::SaveGame_Begin);
    printSaveDataText();
}

void SystemStateMachine::beginLoadGame() {
    printLine(QStringLiteral("要读取第几号？"));
    setState(SystemStateCode::LoadGame_Begin);
    printSaveDataText();
}

void SystemStateMachine::beginLoadGameOpening() {
    printLine(QStringLiteral("要读取第几号？"));
    setState(SystemStateCode::LoadGameOpenning_Begin);
    printSaveDataText();
}

bool SystemStateMachine::writeSavedataTextFrom(int index) {
    if (!m_host.checkData) {
        return false;
    }
    QString message;
    const bool ok = m_host.checkData(index, &message);
    if (!message.isEmpty()) {
        if (m_host.print) m_host.print(message);
        if (m_host.newLine) m_host.newLine();
    }
    return ok;
}

void SystemStateMachine::printSaveDataText() {
    if (m_isFirstTime) {
        m_isFirstTime = false;
        m_dataIsAvailable = QList<bool>(saveDataNos() + 1, false);
    }
    if (m_dataIsAvailable.isEmpty()) {
        m_dataIsAvailable = QList<bool>(saveDataNos() + 1, false);
    }

    const int total = m_dataIsAvailable.size();
    for (int i = 0; i < m_page; ++i) {
        flushOut();
        printLine(QStringLiteral("[%1] 显示存档数据%2～%3")
                      .arg(i * 20, 2).arg(i * 20, 2).arg(i * 20 + 19, 2));
    }
    for (int i = 0; i < 20; ++i) {
        const int dataNo = m_page * 20 + i;
        if (dataNo == total - 1) break;
        m_dataIsAvailable[dataNo] = false;
        flushOut();
        printCn(QStringLiteral("[%1] ").arg(dataNo, 2), false);
        if (!writeSavedataTextFrom(dataNo)) continue;
        m_dataIsAvailable[dataNo] = true;
    }
    for (int i = m_page; i < (total - 2) / 20; ++i) {
        flushOut();
        printLine(QStringLiteral("[%1] 显示存档数据%2～%3")
                      .arg((i + 1) * 20, 2).arg((i + 1) * 20, 2).arg((i + 1) * 20 + 19, 2));
    }

    // 自动存档单独处理（显示顺序）
    m_dataIsAvailable[total - 1] = false;
    if (m_state->getSystemState() != SystemStateCode::SaveGame_Begin) {
        flushOut();
        printCn(QStringLiteral("[%1] ").arg(kAutoSaveIndex, 2), false);
        if (writeSavedataTextFrom(kAutoSaveIndex)) {
            m_dataIsAvailable[total - 1] = true;
        }
    }
    refresh();
    printLine(QStringLiteral("[100] 返回"));

    setWaitInput();
    if (m_state->getSystemState() == SystemStateCode::SaveGame_Begin) {
        setState(SystemStateCode::SaveGame_WaitInput);
    } else if (m_state->getSystemState() == SystemStateCode::LoadGame_Begin) {
        setState(SystemStateCode::LoadGame_WaitInput);
    } else {
        setState(SystemStateCode::LoadGameOpenning_WaitInput);
    }
}

void SystemStateMachine::saveGameWaitInput() {
    const int total = m_dataIsAvailable.size();
    if (m_systemResult == 100) {
        // 取消：回到上一个状态
        loadPrevState();
        return;
    }
    if ((m_systemResult / 20) != m_page && m_systemResult != kAutoSaveIndex
        && m_systemResult >= 0 && m_systemResult < total - 1) {
        m_page = static_cast<int>(m_systemResult / 20);
        setState(SystemStateCode::SaveGame_Begin);
        printSaveDataText();
        return;
    }

    bool available = false;
    if (m_systemResult >= 0 && m_systemResult < total - 1) {
        available = m_dataIsAvailable.at(m_systemResult);
    } else {
        deleteLines(1);
        printTemporary(QStringLiteral("无效的值"));
        setWaitInput();
        return;
    }
    m_saveTarget = static_cast<int>(m_systemResult);
    if (available) {
        printLine(QStringLiteral("已存在数据，是否覆盖？"));
        printCn(QStringLiteral("[0] 是"), false);
        printCn(QStringLiteral("[1] 否"), false);
        setWaitInput();
        setState(SystemStateCode::SaveGame_WaitInputOverwrite);
        return;
    }
    m_systemResult = 0;
    saveGameWaitInputOverwrite();
}

void SystemStateMachine::saveGameWaitInputOverwrite() {
    if (m_systemResult == 1) {          // 否
        beginSaveGame();
        return;
    }
    if (m_systemResult != 0) {          // 非法输入
        deleteLines(1);
        printTemporary(QStringLiteral("无效的值"));
        setWaitInput();
        return;
    }
    setState(SystemStateCode::SaveGame_CallSaveInfo);
    if (!callFunction(QStringLiteral("SAVEINFO"), false, false)) {
        endCallSaveInfo();
    }
}

void SystemStateMachine::endCallSaveInfo() {
    const QString text = startDateText();
    if (!(m_host.saveTo && m_host.saveTo(m_saveTarget, text))) {
        printErrorLine(QStringLiteral("保存时发生意外错误"));
        if (m_host.readAnyKey) m_host.readAnyKey();
    }
    loadPrevState();
}

void SystemStateMachine::loadGameWaitInput() {
    const int total = m_dataIsAvailable.size();
    if (m_systemResult == 100) {
        if (m_state->getSystemState() == SystemStateCode::LoadGameOpenning_WaitInput) {
            beginTitle();
        } else {
            // C# loadPrevState 恢复整份备份进程（含调用栈）；引擎只有状态码快照，
            // 残留的旧脚本帧无法按行复活。从 Normal（标题）打开的读档画面取消 =
            // 清掉残留帧、回到标题重画（对齐可观察行为：重画标题、重新等输入）。
            const SystemStateCode prev = m_prevStates.isEmpty()
                ? SystemStateCode::Title_Begin : m_prevStates.last();
            if (prev == SystemStateCode::Normal) {
                if (!m_prevStates.isEmpty()) m_prevStates.removeLast();
                if (m_table) m_table->resetPosition();
                setState(SystemStateCode::Title_Begin);
            } else {
                loadPrevState();
            }
        }
        return;
    }
    if ((m_systemResult / 20) != m_page && m_systemResult != kAutoSaveIndex
        && m_systemResult >= 0 && m_systemResult < total - 1) {
        m_page = static_cast<int>(m_systemResult / 20);
        setState(m_state->getSystemState() == SystemStateCode::LoadGameOpenning_WaitInput
                     ? SystemStateCode::LoadGameOpenning_Begin
                     : SystemStateCode::LoadGame_Begin);
        printSaveDataText();
        return;
    }

    bool available = false;
    if (m_systemResult >= 0 && m_systemResult < total - 1) {
        available = m_dataIsAvailable.at(m_systemResult);
    } else if (m_systemResult == kAutoSaveIndex) {
        available = m_dataIsAvailable.value(total - 1, false);
    } else {
        deleteLines(1);
        printTemporary(QStringLiteral("无效的值"));
        setWaitInput();
        return;
    }

    if (!available) {
        printLine(QString::number(m_systemResult));
        printErrorLine(QStringLiteral("没有数据"));
        if (m_state->getSystemState() == SystemStateCode::LoadGameOpenning_WaitInput) {
            beginLoadGameOpening();
        } else {
            beginLoadGame();
        }
        return;
    }

    if (!(m_host.loadFrom && m_host.loadFrom(static_cast<int>(m_systemResult)))) {
        emit errorOccurred(QStringLiteral("读取文件时发生意外错误"));
        m_state->setErrorState();
        return;
    }
    // 载入成功：读档前的旧调用栈一并废弃 —— C# 里备份进程被 deletePrevState
    // 丢弃，载入后的游戏从 EVENTLOAD 起走全新流程；残留旧帧（如标题循环）
    // 若不清掉，会在 SHOW_SHOP 返回后复活（标题重画进载入后的游戏）。
    if (m_table) m_table->resetPosition();
    deletePrevState();
    beginDataLoaded();
}

void SystemStateMachine::endNormal() {
    emit errorOccurred(QStringLiteral("意外的脚本终结点"));
    m_state->setErrorState();
}

void SystemStateMachine::endReloaderb() {
    loadPrevState();
}
