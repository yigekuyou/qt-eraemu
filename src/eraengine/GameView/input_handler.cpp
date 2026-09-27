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
#include "input_handler.h"

InputHandler::InputHandler(QObject *parent)
    : QObject(parent),
      m_inputResult(0),
      m_currentInputType(INPUT_NUMERIC),
      m_inputReceived(false)
{
}

void InputHandler::setInputCallback(const QString& callback)
{
    QMutexLocker locker(&m_mutex);
    m_inputCallback = callback;
}

void InputHandler::setInputResult(int result)
{
    QMutexLocker locker(&m_mutex);
    m_inputResult = result;
    m_inputReceived = true;
    m_waitCondition.wakeAll();
}

int InputHandler::waitForInput()
{
    QMutexLocker locker(&m_mutex);
    
    // Wait for input if not already received
    while (!m_inputReceived) {
        m_waitCondition.wait(&m_mutex);
    }
    
    // Reset for next input
    m_inputReceived = false;
    
    return m_inputResult;
}

void InputHandler::reset()
{
    QMutexLocker locker(&m_mutex);
    m_inputReceived = false;
    m_inputResult = 0;
}

// ---------------------------------------------------------------------------
// EraTetrisInputSystem
// ---------------------------------------------------------------------------
EraTetrisInputSystem::EraTetrisInputSystem(QObject *parent) : QObject(parent) {}

bool EraTetrisInputSystem::handleTetrisInput(const QString& input) {
		if (input == "LEFT") { emit tetrisMoveLeft(); return true; }
		if (input == "RIGHT") { emit tetrisMoveRight(); return true; }
		if (input == "DOWN") { emit tetrisMoveDown(); return true; }
		if (input == "ROTATE") { emit tetrisRotate(); return true; }
		if (input == "DROP") { emit tetrisHardDrop(); return true; }
		if (input == "PAUSE") { emit tetrisPause(); return true; }
		if (input == "START") { emit tetrisStart(); return true; }
		return false;
}
