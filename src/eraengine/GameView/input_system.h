#ifndef INPUT_SYSTEM_H
#define INPUT_SYSTEM_H

#include <QObject>
#include <QString>

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

#endif // INPUT_SYSTEM_H