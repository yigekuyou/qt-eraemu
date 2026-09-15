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

#endif // INPUT_HANDLER_H
