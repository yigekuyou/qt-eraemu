#include "input_system.h"

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