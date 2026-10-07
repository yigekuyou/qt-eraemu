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
#include "process_state.h"
#include <QString>
#include <QDebug>

ProcessState::ProcessState(QObject* parent)
    : QObject(parent),
      m_systemState(SystemStateCode::Title_Begin),
      m_execState(ExecState::Continue),
      m_beginType(BeginType::NONE),
      m_currentLine(nullptr),
      m_errorLine(nullptr),
      m_lineCount(0)
{
}

SystemStateCode ProcessState::getSystemState() const {
    return m_systemState;
}

void ProcessState::setSystemState(SystemStateCode state) {
    if (m_systemState != state) {
        m_systemState = state;
        emit stateChanged();
    }
}

SystemStateCode ProcessState::getState() const {
    return m_systemState;
}

void ProcessState::setState(SystemStateCode state) {
    if (m_systemState != state) {
        m_systemState = state;
        emit stateChanged();
    }
}

// ---------------------------------------------------------------------------
// 中心执行状态
// ---------------------------------------------------------------------------
ExecState ProcessState::getExecState() const {
    return m_execState;
}

void ProcessState::setExecState(ExecState state) {
    if (m_execState != state) {
        m_execState = state;
        emit execStateChanged(m_execState);
    }
}

bool ProcessState::isRunning() const {
    return m_execState == ExecState::Continue;
}

void ProcessState::requestWaitInput() {
    setExecState(ExecState::WaitInput);
}

void ProcessState::requestWaitSystemInput() {
    setExecState(ExecState::WaitSystemInput);
}

void ProcessState::requestHalt() {
    setExecState(ExecState::Halt);
}

void ProcessState::requestResume() {
    setExecState(ExecState::Continue);
    // 信号与槽驱动：通知执行链继续
    emit continueExecution();
}

void ProcessState::setErrorState() {
    setExecState(ExecState::Error);
}

BeginType ProcessState::getBeginType() const {
    return m_beginType;
}

void ProcessState::setBegin(BeginType type) {
    m_beginType = type;
}

bool ProcessState::canSave() const {
    return (static_cast<int>(m_systemState) & static_cast<int>(SystemStateCode::__CAN_SAVE__)) != 0;
}

bool ProcessState::canBegin() const {
    return (static_cast<int>(m_systemState) & static_cast<int>(SystemStateCode::__CAN_BEGIN__)) != 0;
}

// ---------------------------------------------------------------------------
// BEGIN 状态迁移（对齐 C# Process.SetBegin / ProcessState.Begin）
// ---------------------------------------------------------------------------

bool ProcessState::setBeginKeyword(const QString& keyword, QString* error,
                                   const QString& funcName, bool force) {
    const QString upperKeyword = keyword.trimmed().toUpper();
    BeginType type = BeginType::NONE;
    if (upperKeyword == QLatin1String("SHOP")) type = BeginType::SHOP;
    else if (upperKeyword == QLatin1String("TRAIN")) type = BeginType::TRAIN;
    else if (upperKeyword == QLatin1String("AFTERTRAIN")) type = BeginType::AFTERTRAIN;
    else if (upperKeyword == QLatin1String("ABLUP")) type = BeginType::ABLUP;
    else if (upperKeyword == QLatin1String("TURNEND")) type = BeginType::TURNEND;
    else if (upperKeyword == QLatin1String("FIRST")) type = BeginType::FIRST;
    else if (upperKeyword == QLatin1String("TITLE")) type = BeginType::TITLE;
    else {
        // 关键字合法性先于 force 检查（C# SetBegin(keyword, force) 的 switch
        // 不认识关键字一律 InvalidBeginArg —— FORCE_BEGIN 传非法关键字同样报错）
        if (error) *error = QStringLiteral("BEGIN 的关键字\"%1\"未定义").arg(keyword);
        return false;
    }
    return processBegin(type, error, funcName, force);
}

bool ProcessState::processBegin(BeginType type, QString* error, const QString& funcName,
                                bool force) {
    // SetBegin(BeginType)：除 TITLE 外都要求当前状态允许 BEGIN
    // （C#：SHOP/TRAIN/AFTERTRAIN/ABLUP/TURNEND/FIRST 需 __CAN_BEGIN__；
    //  1.729 起 BEGIN TITLE 在任何状态都可用；force=true 跳过检查 —— EE
    //  FORCE_BEGIN，Process.State.cs:216 的 `if (force == true) break;`）
    if (type != BeginType::TITLE && type != BeginType::NONE && !force && !canBegin()) {
        if (error) {
            QString name = funcName;
            if (name.isEmpty() && !m_functionList.isEmpty()) {
                name = m_functionList.first().getLabelName();
            }
            *error = QStringLiteral("@%1 中不能执行 BEGIN 命令").arg(name);
        }
        return false;
    }
    m_beginType = type;
    beginFromType();
    return true;
}

void ProcessState::beginFromType() {
    // C# 备注：从 @EVENTSHOP 发起的 BEGIN 一律丢弃
    if (m_systemState == SystemStateCode::Shop_CallEventShop) {
        return;
    }

    switch (m_beginType) {
        case BeginType::SHOP:
            m_calledWhenNormal = (m_systemState == SystemStateCode::Normal);
            m_systemState = SystemStateCode::Shop_Begin;
            break;
        case BeginType::TRAIN:
            m_systemState = SystemStateCode::Train_Begin;
            break;
        case BeginType::AFTERTRAIN:
            m_systemState = SystemStateCode::AfterTrain_Begin;
            break;
        case BeginType::ABLUP:
            m_systemState = SystemStateCode::Ablup_Begin;
            break;
        case BeginType::TURNEND:
            m_systemState = SystemStateCode::Turnend_Begin;
            break;
        case BeginType::FIRST:
            m_systemState = SystemStateCode::First_Begin;
            break;
        case BeginType::TITLE:
            m_systemState = SystemStateCode::Title_Begin;
            break;
        case BeginType::NONE:
        default:
            return;
    }

    clearFunctionList();
    m_beginType = BeginType::NONE;
    emit stateChanged();
}

void ProcessState::clearFunctionList() {
    m_functionList.clear();
    m_beginType = BeginType::NONE;
}

void ProcessState::setBegin(const QString& keyword) {
    QString error;
    if (!setBeginKeyword(keyword, &error)) {
        qWarning().noquote() << "[BEGIN]" << error;
    }
}

bool ProcessState::calledWhenNormal() const { return m_calledWhenNormal; }
void ProcessState::setCalledWhenNormal(bool value) { m_calledWhenNormal = value; }

int ProcessState::currentMin() const { return m_currentMin; }
void ProcessState::setCurrentMin(int value) { m_currentMin = value; }
int ProcessState::functionCount() const { return m_functionList.size(); }

LogicalLine* ProcessState::getCurrentLine() const {
    return m_currentLine;
}

void ProcessState::setCurrentLine(LogicalLine* line) {
    m_currentLine = line;
}

LogicalLine* ProcessState::getErrorLine() const {
    return m_errorLine ? m_errorLine : m_currentLine;
}

void ProcessState::setErrorLine(LogicalLine* line) {
    m_errorLine = line;
}

void ProcessState::pushFunction(const QString& label, const ScriptPosition& pos) {
    m_functionList.append(CalledFunction(label, pos));
}

void ProcessState::popFunction() {
    if (!m_functionList.isEmpty()) {
        m_functionList.removeLast();
    }
}

// IntoFunction: Push function to stack and set current line for execution
void ProcessState::intoFunction(const QString& label, const ScriptPosition& pos) {
    m_functionList.append(CalledFunction(label, pos));
    // Note: We don't set m_currentLine here because the ExecutionEngine
    // manages its own execution queue. This is for state tracking only.
}

// ReturnF: Pop function from stack and return to caller
void ProcessState::returnF() {
    if (!m_functionList.isEmpty()) {
        m_functionList.removeLast();
    }
}

// Get the position of the current function (top of stack)
ScriptPosition ProcessState::getCurrentFunctionPosition() const {
    if (m_functionList.isEmpty()) {
        return ScriptPosition();
    }
    return m_functionList.last().getPosition();
}

// Get label name of current function
QString ProcessState::getCurrentFunctionLabel() const {
    if (m_functionList.isEmpty()) {
        return "";
    }
    return m_functionList.last().getLabelName();
}

int ProcessState::getFunctionCount() const {
    return m_functionList.count();
}

int ProcessState::getLineCount() const {
    return m_lineCount;
}

void ProcessState::setLineCount(int count) {
    m_lineCount = count;
}

bool ProcessState::isScriptEnd() const {
    // 对齐 C#：ScriptEnd = (functionList.Count == currentMin)
    return m_functionList.size() == m_currentMin;
}

bool ProcessState::isBegun() const {
    return m_beginType != BeginType::NONE;
}

void ProcessState::setEntryPointScript(const QString& scriptName) {
    m_entryPointScript = scriptName;
    qDebug() << "Set entry point script:" << scriptName;
}
