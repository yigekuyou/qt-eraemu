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
#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <QObject>
#include <QString>
#include <QFuture>
#include <QMutex>
#include <QWaitCondition>

class InputHandler : public QObject
{
    Q_OBJECT

public:
    explicit InputHandler(QObject *parent = nullptr);
    
    // Input methods
    Q_INVOKABLE void setInputCallback(const QString& callback);
    Q_INVOKABLE void setInputResult(int result);
    Q_INVOKABLE int waitForInput();
    
    // Input types
    enum InputType { INPUT_NUMERIC, INPUT_STRING, INPUT_BUTTON };
    
    // Get current input type
    InputType getCurrentInputType() const { return m_currentInputType; }
    
    // Reset input state
    void reset();

private:
    QString m_inputCallback;
    int m_inputResult;
    InputType m_currentInputType;
    bool m_inputReceived;
    QMutex m_mutex;
    QWaitCondition m_waitCondition;
};

// ---------------------------------------------------------------------------
// EraTetrisInputSystem: maps QML text input to game actions.
// Formerly GameView/input_system.h/.cpp; kept together with InputHandler since
// both are the "user input" surface of the engine.
// ---------------------------------------------------------------------------
class EraTetrisInputSystem : public QObject
{
		Q_OBJECT

public:
		explicit EraTetrisInputSystem(QObject *parent = nullptr);

		// QML 中直接调用的输入动作
		Q_INVOKABLE bool handleTetrisInput(const QString& input);

signals:
		void tetrisMoveLeft();
		void tetrisMoveRight();
		void tetrisMoveDown();
		void tetrisRotate();
		void tetrisHardDrop();
		void tetrisPause();
		void tetrisStart();
};

#endif // INPUT_HANDLER_H
