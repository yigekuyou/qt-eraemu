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
