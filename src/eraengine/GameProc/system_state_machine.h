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
#ifndef SYSTEM_STATE_MACHINE_H
#define SYSTEM_STATE_MACHINE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <functional>
#include <utility>

#include "process_state.h"
#include "era_parse_table.h"

class ScriptRunner;
class VariableStorage;

// ---------------------------------------------------------------------------
// SystemHost —— 系统状态机对「外部世界」的全部依赖
//
// 每一项都是可空的 std::function：默认空 = 无操作 / 中性值，因此单元测试
// 只需要覆盖自己关心的回调，其余保持默认即可。EraEngine 负责把控制台、配置、
// 存档、角色数据等真实子系统接到这里。
// ---------------------------------------------------------------------------
struct SystemHost {
    // ---- 输出（对齐 C# EmueraConsole）----
    std::function<void(const QString&)>        printSingleLine;      // 独占一行
    std::function<void(const QString&)>        print;                // 不换行
    std::function<void(const QString&, bool)>  printC;               // 不换行(列, 是否刷新)
    std::function<void(const QString&)>        printTemporaryLine;   // 临时行（可被覆盖）
    std::function<void(const QString&)>        printError;
    std::function<void(int)>                   deleteLine;
    std::function<void()>                      printBar;
    std::function<void()>                      newLine;
    std::function<void(int)>                   setAlignment;         // 0=左 1=中 2=右
    std::function<void()>                      refreshStrings;
    std::function<void()>                      printFlush;

    // ---- 输入等待（通知 UI；真正的等待由中心执行状态 ExecState 表达）----
    std::function<void()>                      waitInput;   // 请求整数输入
    std::function<void()>                      readAnyKey;  // 请求任意键

    // ---- 控制台末行状态（对齐 C# EmueraConsole.LastLineIsTemporary/LastLineIsEmpty）----
    std::function<bool()>                      lastLineIsTemporary;
    std::function<bool()>                      lastLineIsEmpty;

    // ---- 数据层（角色 / 商店 / 存档）----
    std::function<void()>                      resetData;
    std::function<void(int)>                   addCharacterFromCsvNo;
    std::function<int()>                       defaultCharacter;
    std::function<bool(int)>                   itemSales;
    std::function<bool(int)>                   buyItem;
    std::function<bool(int, const QString&)>   saveTo;       // (编号, 存档文本) -> 成功
    std::function<bool(int)>                   loadFrom;
    std::function<bool(int, QString*)>         checkData;    // (编号, 出参信息) -> 是否可用
    std::function<int()>                       saveDataNos;

    // ---- 配置（对齐 C# Config.*）----
    std::function<bool()>                      autoSave;
    std::function<int()>                       maxShopItem;
    std::function<int()>                       comAbleDefault;
    std::function<int()>                       printCPerLine;
    std::function<bool()>                      compatiCallEvent;
    std::function<QString(int)>                titleMenuString;   // 0/1

    // ---- GameBase（标准标题画面）----
    std::function<QString()>                   scriptTitle;
    std::function<QString()>                   scriptVersionText;
    std::function<QString()>                   scriptAutherName;
    std::function<QString()>                   scriptYear;
    std::function<QString()>                   scriptDetail;
    std::function<qint64()>                    scriptVersion;

    // ---- TRAIN 命令名（Train.csv 第 0 列；空 = 没有 Train.csv）----
    std::function<QStringList()>               trainNames;
};

// ---------------------------------------------------------------------------
// SystemStateMachine —— 系统状态机（对齐 C# Process.SystemProc）
//
// 职责：
//   * 维护 SystemStateCode -> 处理函数 的调度表（C# systemProcessDictionary，42 条）；
//   * 在「脚本执行到帧底」时运行对应状态的处理函数（C# runSystemProc）；
//   * 处理函数通过 callFunction() 调用 ERB 函数（@EVENTTRAIN / @SHOW_STATUS / …），
//     于是脚本与状态机交替推进 —— 这就是 C# DoScript 的门控循环；
//   * 系统输入等待用中心执行状态 ProcessState::ExecState::WaitSystemInput 表达，
//     用户交付输入后 resume() 继续驱动对应状态的处理函数。
//
// 与 C# 的差异（有意为之）：
//   * C# 用阻塞式 console.WaitInput，本移植改为 ExecState 挂起 + 信号恢复；
//   * 存档/角色/商店等子系统尚未落地，通过 SystemHost 回调抽象，默认中性实现。
// ---------------------------------------------------------------------------
class SystemStateMachine : public QObject {
    Q_OBJECT

public:
    using Handler = std::function<void()>;

    SystemStateMachine(ProcessState* state, EraParseTable* table = nullptr, QObject* parent = nullptr);
    ~SystemStateMachine() override;

    // ---- 依赖注入 ----
    void setHost(SystemHost host) { m_host = std::move(host); }
    [[nodiscard]] SystemHost& host() { return m_host; }
    void setParseTable(EraParseTable* table) { m_table = table; }
    void setVariableStorage(VariableStorage* storage) { m_storage = storage; }
    void setScriptRunner(ScriptRunner* runner) { m_runner = runner; }

    // 装载完成后调用一次：构建状态表 + 读取 Train.csv 命令名。
    void initialize();
    // 重新读取 TRAIN 命令名（Train.csv 变化后调用）
    void reloadTrainNames();

    // ---- 驱动 ----
    // 从当前状态开始，交替执行脚本与状态处理器，直到挂起/停止/出错。
    ExecState run();
    // 用户交付整数输入后继续（写入 RESULT/systemResult）
    ExecState resume(qint64 value);
    // 用户交付字符串输入后继续
    ExecState resumeString(const QString& value);
    // 交付「多值输入」（INPUTMOUSEKEY：RESULT:0..4 = 类型/坐标/按键）
    ExecState deliverInputValues(const QList<qint64>& values);

    // ---- 实时/限时输入（对齐 C# AWAIT / INPUTMOUSEKEY / TONEINPUT）----
    // 计时器注入：GUI 用 QTimer；测试可手动触发。未注入时回调**立即**执行（无延迟）。
    using TimerFn = std::function<void(int ms, std::function<void()> cb)>;
    void setTimer(TimerFn fn) { m_timer = std::move(fn); }
    // AWAIT n：挂起 n 毫秒后自动继续（n<=0 立即继续）
    void awaitDelay(int ms);
    // INPUTMOUSEKEY [time]：等待鼠标/键盘；time>0 超时按 [4,0,0,0,0] 返回
    void waitMouseKey(int timeoutMs);
    // TONEINPUT time：限时输入；超时按默认值（RESULT=0）返回
    void waitTimedInput(int timeoutMs);

    // C# Process.runSystemProc：对当前状态执行一次处理函数。
    void runSystemProc();

    // ---- 查询 ----
    [[nodiscard]] bool hasHandler(SystemStateCode state) const { return m_handlers.contains(state); }
    [[nodiscard]] int handlerCount() const { return m_handlers.size(); }
    [[nodiscard]] const QHash<SystemStateCode, Handler>& handlers() const { return m_handlers; }
    [[nodiscard]] qint64 systemResult() const { return m_systemResult; }
    void setSystemResult(qint64 value) { m_systemResult = value; }

    // 脚本是否停在「系统层」（函数栈回到帧底，等待状态机接管）
    [[nodiscard]] bool atSystemFloor() const { return m_atFloor; }

    // 事件调用状态（供测试/调试）
    [[nodiscard]] bool inEventCall() const { return m_event.active; }
    [[nodiscard]] QString currentEventLabel() const { return m_event.current.script; }
    [[nodiscard]] int eventGroup() const { return m_event.group; }

    // ---- BEGIN ----
    bool beginWithKeyword(const QString& keyword, QString* error = nullptr);
    bool beginWithType(BeginType type, QString* error = nullptr);

    // ---- CALLTRAIN / STOPCALLTRAIN / DOTRAIN 指令钩子 ----
    void setCommands(qint64 count);      // CALLTRAIN
    bool clearCommands();                // STOPCALLTRAIN / 内部
    void setDoTrainSelectCom(qint64 com) { m_doTrainSelectCom = com; }
    [[nodiscard]] bool isContinuousTrain() const { return m_isCTrain; }
    [[nodiscard]] int trainCount() const { return m_trainNames.size(); }
    void doTrain();                      // DOTRAIN

    // ---- SAVEGAME / LOADGAME 指令钩子 ----
    // 记录当前状态作为「返回状态」，并把系统状态切到 SaveGame_Begin / LoadGame_Begin。
    void requestSaveLoad(bool save);
    void loadPrevState();
    void deletePrevState();

    // ---- 静态元信息 ----
    [[nodiscard]] static QString stateName(SystemStateCode state);
    [[nodiscard]] static QString stateDescription(SystemStateCode state);
    [[nodiscard]] static bool isSavePermitted(SystemStateCode state);
    [[nodiscard]] static bool isBeginPermitted(SystemStateCode state);

signals:
    void stateChanged(SystemStateCode oldState, SystemStateCode newState);
    void inputRequested(SystemStateCode state);   // 请求系统输入（TRAIN/SHOP/…）
    void stateAdvanced(SystemStateCode state);    // 每次处理函数执行后
    void errorOccurred(const QString& message);

private:
    // ---- 事件调用（对齐 C# CalledFunction 的 4 组导航）----
    struct EventCall {
        bool     active = false;
        QString  name;
        QList<QList<LabelRef>> groups;   // [0]=#ONLY [1]=#PRI [2]=普通 [3]=#LATER
        int      group = 0;
        int      counter = -1;
        LabelRef current;
        bool     hasCurrent = false;
        bool     isOnly = false;
        bool     hasSingleFlag = false;
    };

    // ---- 状态处理器（C# Process.SystemProc.cs 一一对应）----
    void beginTitle();
    void endOpenning();
    void beginFirst();
    void endTitleLoadgame();
    void beginTrain();
    void endCallEventTrain();
    void endCallShowStatus();
    void endCallComAbleXX();
    void endCallShowUserCom();
    void trainWaitInput();
    void endEventCom();
    void endCallComXX();
    void endCallSourceCheck();
    void endCallEventComEnd();
    void beginAfterTrain();
    void beginAblup();
    void endCallShowJuel();
    void endCallShowAblupSelect();
    void ablupWaitInput();
    void endCallAblupXX();
    void beginTurnend();
    void beginShop();
    void endCallEventShop();
    void beginAutoSave();
    void endAutoSaveCallSaveInfo();
    void endAutoSave();
    void endCallShowShop();
    void shopWaitInput();
    void endCallEventBuy();
    void beginDataLoaded();
    void endSystemLoad();
    void endEventLoad();
    void beginSaveGame();
    void beginLoadGame();
    void beginLoadGameOpening();
    void printSaveDataText();
    void saveGameWaitInput();
    void saveGameWaitInputOverwrite();
    void endCallSaveInfo();
    void loadGameWaitInput();
    void endNormal();
    void endReloaderb();

    // ---- 内部工具 ----
    bool callFunction(const QString& name, bool force, bool isEvent);
    // 跳到某个具体标签（系统层调用：返回脚本 = 该脚本自身，返回地址 = 脚本末尾）
    bool callLabelRef(const LabelRef& ref);
    ExecState pump();                 // 门控循环（脚本层 <-> 系统层）
    bool advanceEventCall();          // 事件函数返回后推进到下一个同名函数
    void clearEventCall();
    void eventShiftNext();
    void eventShiftNextGroup();
    void refreshEventCurrentFlags();
    void callEventCom();              // C# callEventCom
    void openingInput();              // C# openingInput
    int  saveDataNos() const;         // 存档位数（Config.SaveDataNos，缺省 20）
    bool writeSavedataTextFrom(int index);
    void setWaitInput();
    void setWait();
    void setState(SystemStateCode state);
    void printLine(const QString& text);
    void printCn(const QString& text, bool flush);
    void printTemporary(const QString& text);
    void printErrorLine(const QString& text);
    void deleteLines(int count);
    void refresh();
    void flushOut();
    QString comString(int trainCode, int comNo) const;
    int  intConfig(const std::function<int()>& fn, int fallback) const;
    bool boolConfig(const std::function<bool()>& fn, bool fallback) const;
    QString startDateText() const;

    ProcessState*    m_state = nullptr;
    EraParseTable*   m_table = nullptr;
    ScriptRunner*    m_runner = nullptr;
    VariableStorage* m_storage = nullptr;
    SystemHost       m_host;
    TimerFn          m_timer;

    QHash<SystemStateCode, Handler> m_handlers;
    bool m_initialized = false;
    bool m_pumpActive = false;
    bool m_atFloor = false;      // true = 函数栈回到帧底（系统层）

    // ---- 运行期状态（对齐 C# Process 的成员）----
    qint64 m_systemResult = 0;
    QList<int> m_comAble;        // TrainName 下标 -> 显示编号
    QList<int> m_coms;           // 连续调教命令列表（SELECTCOM[1..]）
    int  m_lastCalledComable = -1;
    int  m_lastAddCom = -1;
    int  m_printComCount = 0;
    bool m_isCTrain = false;
    int  m_count = 0;
    bool m_skipPrint = false;
    bool m_needWaitToEventComEnd = false;
    bool m_needCheck = true;
    qint64 m_doTrainSelectCom = -1;
    int  m_saveTarget = -1;
    bool m_isFirstTime = true;
    int  m_page = 0;
    QList<bool> m_dataIsAvailable;
    QStringList m_trainNames;

    EventCall m_event;
    QList<SystemStateCode> m_prevStates;   // SAVEGAME/LOADGAME 的返回状态栈
};

#endif // SYSTEM_STATE_MACHINE_H
