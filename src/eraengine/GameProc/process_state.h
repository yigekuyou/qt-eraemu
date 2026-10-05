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
#ifndef PROCESS_STATE_H
#define PROCESS_STATE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QMetaType>
#include "ast/logical_line.h"

// Forward declarations
class LogicalLine;

// System state codes matching C# SystemStateCode
// These states track the current phase of the Emuera game execution
enum class SystemStateCode {
    // Can save flag
    __CAN_SAVE__ = 0x10000,
    
    // Can begin flag  
    __CAN_BEGIN__ = 0x20000,
    
    // Initial state
    Title_Begin = 0,
    
    // Opening phase
    Openning = 1,
    
    // Normal game flow
    Normal = 0xFFFF | __CAN_BEGIN__ | __CAN_SAVE__,
    
    // TRAIN system states
    Train_Begin = 0x10,
    Train_CallEventTrain = 0x11,
    Train_CallShowStatus = 0x12,
    Train_CallComAbleXX = 0x13,
    Train_CallShowUserCom = 0x14,
    Train_WaitInput = 0x15,
    Train_CallEventCom = 0x16 | __CAN_BEGIN__,
    Train_CallComXX = 0x17 | __CAN_BEGIN__,
    Train_CallSourceCheck = 0x18 | __CAN_BEGIN__,
    Train_CallEventComEnd = 0x19 | __CAN_BEGIN__,
    Train_DoTrain = 0x1A,
    
    // AFTERTRAIN system states
    AfterTrain_Begin = 0x20 | __CAN_BEGIN__,
    
    // ABLUP system states
    Ablup_Begin = 0x30,
    Ablup_CallShowJuel = 0x31,
    Ablup_CallShowAblupSelect = 0x32,
    Ablup_WaitInput = 0x33,
    Ablup_CallAblupXX = 0x34 | __CAN_BEGIN__,
    
    // TURNEND system states
    Turnend_Begin = 0x40 | __CAN_BEGIN__,
    
    // SHOP system states
    Shop_Begin = 0x50 | __CAN_SAVE__,
    Shop_CallEventShop = 0x51 | __CAN_BEGIN__ | __CAN_SAVE__,
    Shop_CallShowShop = 0x52 | __CAN_SAVE__,
    Shop_WaitInput = 0x53 | __CAN_SAVE__,
    Shop_CallEventBuy = 0x54 | __CAN_BEGIN__ | __CAN_SAVE__,
    
    // SAVE/LOAD system states
    SaveGame_Begin = 0x100,
    SaveGame_WaitInput = 0x101,
    SaveGame_WaitInputOverwrite = 0x102,
    SaveGame_CallSaveInfo = 0x103,
    LoadGame_Begin = 0x110,
    LoadGame_WaitInput = 0x111,
    LoadGameOpenning_Begin = 0x120,
    LoadGameOpenning_WaitInput = 0x121,
    
    // AUTO system states
    AutoSave_CallSaveInfo = 0x201,
    AutoSave_CallUniqueAutosave = 0x202,
    AutoSave_Skipped = 0x203,
    
    // LOAD data states
    LoadData_DataLoaded = 0x210,
    LoadData_CallSystemLoad = 0x211 | __CAN_BEGIN__,
    LoadData_CallEventLoad = 0x212 | __CAN_BEGIN__,
    
    // System states
    Openning_TitleLoadgame = 0x220,
    System_Reloaderb = 0x230,
    First_Begin = 0x240,
};

// Begin types
enum class BeginType {
    NONE = 0,
    SHOP = 2,
    TRAIN = 3,
    AFTERTRAIN = 4,
    ABLUP = 5,
    TURNEND = 6,
    FIRST = 7,
    TITLE = 8,
};

// State codes for backward compatibility
// (using SystemStateCode as the primary state code)
using StateCode = SystemStateCode;

// ---------------------------------------------------------------------------
// 中心执行状态 (Execution state)
//
// 执行链每执行一步都会查询它；由“程序状态控制器”（ProcessState，配合
// SystemProcessor）统一设置。对应 C# Emuera 的 console.IsRunning / ConsoleState
// 与 Process 的 DoScript 门控：一旦不再是 Continue，执行链立即挂起并返回。
//
//   Continue        —— 继续执行（C# Running）
//   WaitInput       —— 等待用户操作（普通 INPUT/ONEINPUT/…，C# WaitInput）
//   WaitSystemInput —— 等待系统输入（状态机驱动，如 TRAIN/SHOP 的输入）
//   WaitEvent       —— 等待事件（C# 事件导航）
//   Halt            —— 脚本结束，停止
//   Error           —— 出错停止
// ---------------------------------------------------------------------------
enum class ExecState {
    Continue = 0,
    WaitInput,
    WaitSystemInput,
    WaitEvent,
    Halt,
    Error
};
Q_DECLARE_METATYPE(ExecState)

// Called function information
struct CalledFunction {
    QString labelName;
    ScriptPosition position;
    
    CalledFunction(const QString& label = "", const ScriptPosition& pos = ScriptPosition())
        : labelName(label), position(pos) {}
    
    // Get position for this function
    ScriptPosition getPosition() const { return position; }
    
    // Get label name
    QString getLabelName() const { return labelName; }
};

Q_DECLARE_METATYPE(CalledFunction)

// Process state management
class ProcessState : public QObject {
    Q_OBJECT

public:
    explicit ProcessState(QObject* parent = nullptr);
    
    // State management
    StateCode getState() const;
    void setState(StateCode state);
    
    // Begin type management
    BeginType getBeginType() const;
    void setBegin(BeginType type);
    void setBegin(const QString& keyword);
    
    // System state management
    SystemStateCode getSystemState() const;
    void setSystemState(SystemStateCode state);
    
    // Check if system state allows saving
    bool canSave() const;
    
    // Check if system state allows BEGIN command
    bool canBegin() const;
    
    // Process state management (BEGIN command handling)
    // SetBegin(BeginType) + Begin()：校验 __CAN_BEGIN__ 后立即切换状态。
    bool processBegin(BeginType type, QString* error = nullptr,
                      const QString& funcName = QString());
    
    // Line management
    LogicalLine* getCurrentLine() const;
    void setCurrentLine(LogicalLine* line);
    
    LogicalLine* getErrorLine() const;
    void setErrorLine(LogicalLine* line);
    
    // Function call stack
    void pushFunction(const QString& label, const ScriptPosition& pos);
    void popFunction();
    int getFunctionCount() const;
    
    // Into function and return (for CALL/GOTO/RETURN flow control)
    // IntoFunction: Push function to stack and set current line
    void intoFunction(const QString& label, const ScriptPosition& pos);
    // ReturnF: Pop function from stack and return to caller
    void returnF();
    
    // Line counting
    int getLineCount() const;
    void setLineCount(int count);
    
    // Function stack getters
    ScriptPosition getCurrentFunctionPosition() const;
    QString getCurrentFunctionLabel() const;
    
    // State queries
    //
    // 对齐 C# ProcessState.ScriptEnd = (functionList.Count == currentMin)。
    // currentMin 是「帧底」：0 表示系统层可以调用脚本函数，函数返回后
    // functionList 缩回 currentMin 即视为「脚本执行结束」，由系统状态机接管。
    bool isScriptEnd() const;
    bool isBegun() const;

    // 帧底（C# currentMin）与函数栈规模（C# functionCount）
    int currentMin() const;
    void setCurrentMin(int value);
    int functionCount() const;

    // 清空函数栈并复位 begintype（C# ClearFunctionList）
    void clearFunctionList();

    // C# Process.SetBegin(string)：把关键字解析为 BeginType，未定义则失败
    // funcName：发起 BEGIN 的函数名（用于错误消息，对齐 C# functionList[0].FunctionName）
    bool setBeginKeyword(const QString& keyword, QString* error = nullptr,
                         const QString& funcName = QString());

    // C# ProcessState.Begin()：按已设置的 begintype 切换系统状态、清空函数栈、
    // 复位 begintype。由系统状态机在脚本执行到帧底时调用。
    void beginFromType();

    // C# Process.calledWhenNormal：本次 BEGIN 是否从 Normal 状态发起
    // （自动存档只在 Normal 发起 SHOP 时才做）
    bool calledWhenNormal() const;
    void setCalledWhenNormal(bool value);
    
    // =======================================================================
    // 中心执行状态 (Execution state) —— 由状态控制器设置，执行链查询
    // =======================================================================
    ExecState getExecState() const;
    void setExecState(ExecState state);

    // 是否允许执行链继续推进（等价 C# console.IsRunning）
    bool isRunning() const;

    // 请求挂起等待用户输入 / 恢复 / 停止
    void requestWaitInput();
    void requestWaitSystemInput();
    void requestHalt();
    // QUIT：结束整个程序（区别于 Halt —— Halt 只是「脚本回到底层」，
    // 系统状态机随后会重新驱动标题/主循环；QUIT 要求彻底停止）。
    void requestQuit() { m_quitRequested = true; setExecState(ExecState::Halt); }
    [[nodiscard]] bool quitRequested() const { return m_quitRequested; }
    // 重新装载新游戏目录时清掉上一局的 QUIT 请求（m_quitRequested 只在此复位）
    void clearQuitRequest() { m_quitRequested = false; }
    void requestResume();      // 置回 Continue
    void setErrorState();

    // Set the entry point script name for state tracking
    void setEntryPointScript(const QString& scriptName);
    
    bool m_quitRequested = false;

signals:
    // State changed signal - emitted when state changes
    void stateChanged();

    // 中心执行状态变化（执行/等待输入/停止…）
    void execStateChanged(ExecState state);

    // 请求执行链继续（由状态控制器发出，执行链的 onContinueExecution() 槽响应）
    void continueExecution();
    
private:
    SystemStateCode m_systemState;
    ExecState m_execState;
    BeginType m_beginType;
    bool m_calledWhenNormal = true;
    LogicalLine* m_currentLine;
    LogicalLine* m_errorLine;
    int m_lineCount;
    int m_currentMin = 0;
    QList<CalledFunction> m_functionList;
    
    // Entry point script name
    QString m_entryPointScript;
};

#endif // PROCESS_STATE_H
