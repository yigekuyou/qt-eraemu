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
#include "system_processor.h"
#include <QDebug>
#include "process_state.h"
#include "game_base_data.h"
#include "era_parse_table.h"
#include "execution_engine.h"

SystemProcessor::SystemProcessor(ProcessState* state, QObject* parent)
    : QObject(parent), m_state(state), m_parseTable(nullptr), m_executionEngine(nullptr)
{
    if (!m_state) {
        m_state = new ProcessState(this);
    }
}

void SystemProcessor::setParseTable(EraParseTable* parseTable) {
    m_parseTable = parseTable;
    qDebug() << "[SystemProcessor] Parse table connected";
}

void SystemProcessor::setExecutionEngine(ExecutionEngine* engine) {
    m_executionEngine = engine;
    qDebug() << "[SystemProcessor] Execution engine connected";
}

bool SystemProcessor::processInputRequest() {
    // Emit input requested signal with current state
    if (m_state) {
        StateCode currentState = m_state->getState();
        qDebug() << "Input requested for state:" << static_cast<int>(currentState);
        emit inputRequested(currentState);
    }
    // In a real implementation, this would wait for user input
    // For now, just return success
    qDebug() << "Input request processed";
    return true;
}

bool SystemProcessor::processConsoleOutput(const QString& text) {
    // Output to console
    qDebug() << "Console output:" << text;
    emit outputReceived(text);
    return true;
}

bool SystemProcessor::processLabelJump(const QString& label) {
    // Handle label jump
    qDebug() << "Label jump to:" << label;
    return true;
}

bool SystemProcessor::transitionToTitle() {
    setStateCode(StateCode::Title_Begin);
    m_state->setBegin(BeginType::TITLE);
    return true;
}

bool SystemProcessor::transitionToGame() {
    // Transition to normal game state
    setStateCode(StateCode::Normal);
    m_state->setBegin(BeginType::TRAIN);
    return true;
}

bool SystemProcessor::transitionToState(StateCode state) {
    setStateCode(state);
    return true;
}

bool SystemProcessor::transitionToTrain() {
    StateCode oldState = m_state->getState();
    setStateCode(SystemStateCode::Train_Begin);
    m_state->setBegin(BeginType::TRAIN);
    emitStateTransition(oldState, SystemStateCode::Train_Begin);
    return true;
}

bool SystemProcessor::transitionToAblup() {
    StateCode oldState = m_state->getState();
    setStateCode(SystemStateCode::Ablup_Begin);
    m_state->setBegin(BeginType::ABLUP);
    emitStateTransition(oldState, SystemStateCode::Ablup_Begin);
    return true;
}

bool SystemProcessor::transitionToShop() {
    StateCode oldState = m_state->getState();
    setStateCode(SystemStateCode::Shop_Begin);
    m_state->setBegin(BeginType::SHOP);
    emitStateTransition(oldState, SystemStateCode::Shop_Begin);
    return true;
}

bool SystemProcessor::transitionToSaveGame() {
    StateCode oldState = m_state->getState();
    setStateCode(SystemStateCode::SaveGame_Begin);
    emitStateTransition(oldState, SystemStateCode::SaveGame_Begin);
    return true;
}

bool SystemProcessor::transitionToLoadGame() {
    StateCode oldState = m_state->getState();
    setStateCode(SystemStateCode::LoadGame_Begin);
    emitStateTransition(oldState, SystemStateCode::LoadGame_Begin);
    return true;
}

bool SystemProcessor::transitionToAfterTrain() {
    StateCode oldState = m_state->getState();
    setStateCode(SystemStateCode::AfterTrain_Begin);
    emitStateTransition(oldState, SystemStateCode::AfterTrain_Begin);
    return true;
}

bool SystemProcessor::transitionToTurnEnd() {
    StateCode oldState = m_state->getState();
    setStateCode(SystemStateCode::Turnend_Begin);
    emitStateTransition(oldState, SystemStateCode::Turnend_Begin);
    return true;
}

bool SystemProcessor::processLine(const LogicalLine& line) {
    // 处理一条完整的 AST 逻辑行。
    if (line.kind != LineKind::Instruction) {
        m_state->setLineCount(m_state->getLineCount() + 1);
        return true;
    }

    const QString& name = line.functionName;
    const QList<Operand>& args = line.arguments;
    const auto arg0 = [&]() -> QString { return args.isEmpty() ? QString() : args.first().raw; };

    if (name == "BEGIN") {
        if (!args.isEmpty()) {
            m_state->setBegin(arg0());
            checkStateForInputWait();
        }
        qDebug() << "[SystemProcessor] BEGIN instruction processed";
    }
    else if (name == "GOTO") {
        const QString label = arg0();
        processLabelJump(label);
        qDebug() << "[SystemProcessor] GOTO label:" << label;
    }
    else if (name == "IF") {
        qDebug() << "[SystemProcessor] IF statement processed";
    }
    else if (name == "ELSEIF" || name == "ELSE" || name == "ENDIF") {
        qDebug() << "[SystemProcessor] Control flow:" << name;
    }
    else if (name == "FOR" || name == "LOOP" || name == "NEXT") {
        qDebug() << "[SystemProcessor] Loop statement:" << name;
    }
    else if (name == "CALL") {
        if (!args.isEmpty()) {
            qDebug() << "[SystemProcessor] CALL label:" << arg0();
        }
    }
    else if (name == "RETURN") {
        qDebug() << "[SystemProcessor] RETURN instruction";
    }
    else if (name == "WAIT" || name == "INPUT" || name == "SELECT"
             || name == "TONEINPUT" || name == "ONEINPUT") {
        qDebug() << "[SystemProcessor] Input-related instruction:" << name;
        processInputRequest();
    }
    else if (name == "PRINT" || name == "PRINTFORML" || name == "PRINTBUTTON") {
        qDebug() << "[SystemProcessor] Output instruction:" << name;
    }
    else if (name == "RESETDATA" || name == "LOADGLOBAL" || name == "RESETCOLOR") {
        qDebug() << "[SystemProcessor] System instruction:" << name;
    }
    else if (name == "SIF" || name == "ALIGNMENT" || name == "DRAWLINE") {
        qDebug() << "[SystemProcessor] Special instruction:" << name;
    }
    else if (name == "=" || name == "+=" || name == "-=" || name == "*=" || name == "/=") {
        qDebug() << "[SystemProcessor] Assignment:" << name;
    }
    else {
        qDebug() << "[SystemProcessor] Unknown instruction:" << name;
    }

    m_state->setLineCount(m_state->getLineCount() + 1);
    return true;
}

void SystemProcessor::emitStateTransition(StateCode oldState, StateCode newState) {
    qDebug() << "[SystemProcessor] State transition:" << static_cast<int>(oldState) 
             << "->" << static_cast<int>(newState);
    
    // Check if transition involves input wait states
    bool oldNeedsInput = (oldState == SystemStateCode::Train_WaitInput ||
                         oldState == SystemStateCode::Ablup_WaitInput ||
                         oldState == SystemStateCode::Shop_WaitInput ||
                         oldState == SystemStateCode::SaveGame_WaitInput ||
                         oldState == SystemStateCode::LoadGame_WaitInput ||
                         oldState == SystemStateCode::LoadGameOpenning_WaitInput);
    bool newNeedsInput = (newState == SystemStateCode::Train_WaitInput ||
                         newState == SystemStateCode::Ablup_WaitInput ||
                         newState == SystemStateCode::Shop_WaitInput ||
                         newState == SystemStateCode::SaveGame_WaitInput ||
                         newState == SystemStateCode::LoadGame_WaitInput ||
                         newState == SystemStateCode::LoadGameOpenning_WaitInput);
    
    if (oldNeedsInput && !newNeedsInput) {
        // Resumed from input wait
        qDebug() << "[SystemProcessor] Parsing resumed from input wait, new state:" 
                 << static_cast<int>(newState);
        emit parsingResumed(newState);
    } else if (!oldNeedsInput && newNeedsInput) {
        // Paused for input wait
        qDebug() << "[SystemProcessor] Parsing paused for input wait, new state:" 
                 << static_cast<int>(newState);
        emit parsingPaused(newState);
    }
}

void SystemProcessor::checkStateForInputWait() {
    // Check if current state requires input wait and emit appropriate signals
    StateCode state = m_state->getState();
    bool needsInput = (state == SystemStateCode::Train_WaitInput ||
                      state == SystemStateCode::Ablup_WaitInput ||
                      state == SystemStateCode::Shop_WaitInput ||
                      state == SystemStateCode::SaveGame_WaitInput ||
                      state == SystemStateCode::LoadGame_WaitInput ||
                      state == SystemStateCode::LoadGameOpenning_WaitInput);
    
    if (needsInput) {
        qDebug() << "[SystemProcessor] State requires input wait:" << static_cast<int>(state);
        emit inputRequested(state);
    }
}

void SystemProcessor::setStateCode(StateCode code) {
    StateCode oldState = m_state->getState();
    m_state->setState(code);
    emit stateChanged(oldState, code);  // Emit both old and new state
    
    // Emit parsing pause/resume signals based on state changes
    // States that require input wait: _WaitInput states
    bool oldNeedsInput = (oldState == SystemStateCode::Train_WaitInput ||
                         oldState == SystemStateCode::Ablup_WaitInput ||
                         oldState == SystemStateCode::Shop_WaitInput ||
                         oldState == SystemStateCode::SaveGame_WaitInput ||
                         oldState == SystemStateCode::LoadGame_WaitInput ||
                         oldState == SystemStateCode::LoadGameOpenning_WaitInput);
    bool newNeedsInput = (code == SystemStateCode::Train_WaitInput ||
                         code == SystemStateCode::Ablup_WaitInput ||
                         code == SystemStateCode::Shop_WaitInput ||
                         code == SystemStateCode::SaveGame_WaitInput ||
                         code == SystemStateCode::LoadGame_WaitInput ||
                         code == SystemStateCode::LoadGameOpenning_WaitInput);
    
    if (oldNeedsInput && !newNeedsInput) {
        // Resumed from input wait
        qDebug() << "[SystemProcessor] Parsing resumed from input wait, new state:" << static_cast<int>(code);
        emit parsingResumed(code);
    } else if (!oldNeedsInput && newNeedsInput) {
        // Paused for input wait
        qDebug() << "[SystemProcessor] Parsing paused for input wait, new state:" << static_cast<int>(code);
        emit parsingPaused(code);
    }
}

ProcessState* SystemProcessor::getState() const {
    return m_state;
}

void SystemProcessor::onBeginRequested(const QString& keyword) {
    // Get the old state before transition
    StateCode oldState = m_state->getState();
    
    // Set the begin type from keyword
    m_state->setBegin(keyword);
    
    // Process the begin to transition to the correct state
    BeginType type = m_state->getBeginType();
    m_state->processBegin(type);
    
    // Get the new state after transition
    StateCode newState = m_state->getState();
    
    // Emit state changed signal
    emit stateChanged(oldState, newState);
    
    qDebug() << "[SystemProcessor] BEGIN processed:" << keyword 
             << "old state:" << static_cast<int>(oldState)
             << "new state:" << static_cast<int>(newState);
}
