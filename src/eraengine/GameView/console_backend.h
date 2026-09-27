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
#ifndef CONSOLE_BACKEND_H
#define CONSOLE_BACKEND_H

#include <QObject>
#include <QTimer>
#include <QColor>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include "console_types.h"
#include "console_buffer.h"

// ---------------------------------------------------------------------------
// ConsoleBackend —— 显示层的 C++ 后端（C++ 决定“显示什么/怎么显示”）
//
// 刷新模型（对齐 C# EmueraConsole，按本引擎需求放宽到 1Hz）：
//   * 输出只进有界缓冲 + 置 dirty，不做 UI 工作；
//   * 1Hz 定时器兜底：dirty 时 emit windowChanged() 一次（合并多次输出）；
//   * “flush 点”强制刷新：进入 WaitInput / AWAIT / REDRAW / 用户交互等；
//   * 历史有界：超过容量丢最旧；滚动是窗口偏移。
//
// QML 侧只负责：拿可见窗口数据 -> 用模板 createObject -> 转发点击/滚动。
// ---------------------------------------------------------------------------
class ConsoleBackend : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ConsoleBackend)
    QML_UNCREATABLE("ConsoleBackend 由 EraEngine 提供")

    Q_PROPERTY(int  lineCount        READ lineCount        NOTIFY lineCountChanged)
    Q_PROPERTY(int  visibleCount     READ visibleCount     WRITE setVisibleCount     NOTIFY windowChanged)
    Q_PROPERTY(int  scrollOffset     READ scrollOffset     WRITE setScrollOffset     NOTIFY windowChanged)
    Q_PROPERTY(bool followTail       READ followTail                                  NOTIFY windowChanged)
    Q_PROPERTY(int  frameMs          READ frameMs          WRITE setFrameMs          NOTIFY frameMsChanged)
    Q_PROPERTY(QString inputKind     READ inputKind                                  NOTIFY inputRequested)
    Q_PROPERTY(bool waitingInput     READ waitingInput                                NOTIFY waitingInputChanged)

public:
    explicit ConsoleBackend(QObject* parent = nullptr);

    // ---- 只读属性 ----
    int  lineCount() const;
    int  visibleCount() const { return m_visibleCount; }
    int  scrollOffset() const { return m_scrollOffset; }
    bool followTail() const { return m_scrollOffset == 0; }
    int  frameMs() const { return m_frameMs; }
    QString inputKind() const { return m_inputKind; }
    bool waitingInput() const { return m_waitingInput; }

    // ---- 显示写入接口（执行链/指令调用；也开放给 QML/测试）----
    Q_INVOKABLE void print(const QString& text);          // 追加到当前行
    Q_INVOKABLE void newline();                            // 结束当前行
    Q_INVOKABLE void printButton(const QString& text, qint64 value, const QString& tooltip = QString());
    Q_INVOKABLE void printButtonStr(const QString& text, const QString& value, const QString& tooltip = QString());
    Q_INVOKABLE void clearLines(int n);                    // CLEARLINE
    Q_INVOKABLE void clearAll();                           // 清屏
    Q_INVOKABLE void setAlignment(ConsoleAlign align);
    Q_INVOKABLE void setColor(const QColor& color);
    Q_INVOKABLE void resetColor();
    Q_INVOKABLE void setFontStyle(bool bold, bool italic, bool underline, bool strike);

    // 标记“有更新”；1Hz 定时器会合并刷新
    void markDirty();
    // 强制立即刷新（flush 点：进入等待输入前等）
    Q_INVOKABLE void flush();

    // ---- 输入桥接 ----
    void notifyInputRequested(const QString& kind);   // 由执行链调用
    void notifyInputDone();

    // ---- QML 调用 ----
    Q_INVOKABLE QVariantMap visibleLine(int index) const;   // index: [0, visibleLineCount)
    Q_INVOKABLE int  visibleLineCount() const;
    Q_INVOKABLE void clickAt(int visibleIndex, int buttonIndex);
    Q_INVOKABLE void scrollBy(int lines);
    Q_INVOKABLE void scrollToBottom();
    Q_INVOKABLE void tick();                                 // 1Hz 定时器调用
    Q_INVOKABLE void submitInput(qint64 value);
    Q_INVOKABLE void submitInputString(const QString& value);

    // 供 C++/测试
    ConsoleBuffer& buffer() { return m_buffer; }
    const ConsoleBuffer& buffer() const { return m_buffer; }
    quint64 currentGeneration() const { return m_generation; }

signals:
    void windowChanged();          // 可见窗口或内容变化 -> QML 重建/更新
    void lineCountChanged();
    void cleared();
    void frameMsChanged();
    void inputRequested(const QString& kind);  // 需要 QML 提供输入 UI
    void waitingInputChanged();
    void inputSubmitted(qint64 value);          // -> EraEngine::provideInput
    void inputSubmittedString(const QString& value);

public slots:
    void setVisibleCount(int count);
    void setScrollOffset(int offset);
    void setFrameMs(int ms);

private:
    void clampScroll();

    ConsoleBuffer m_buffer;
    ConsoleStyle  m_style;
    ConsoleAlign  m_align = ConsoleAlign::Left;
    bool          m_pendingOpen = false;
    bool          m_dirty = false;

    int  m_visibleCount = 40;
    int  m_scrollOffset = 0;
    int  m_frameMs = 1000;
    quint64 m_generation = 1;

    QString m_inputKind;
    bool    m_waitingInput = false;

    QTimer m_timer;
};

#endif // CONSOLE_BACKEND_H
