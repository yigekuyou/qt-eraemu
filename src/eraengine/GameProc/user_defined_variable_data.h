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
#ifndef USER_DEFINED_VARIABLE_DATA_H
#define USER_DEFINED_VARIABLE_DATA_H

#include <QString>
#include <QList>
#include <QVariant>
#include "ast/logical_line.h"

struct UserDefinedVariableData {
		QString name;
		bool typeIsStr = false;
		bool reference = false;
		int dimension = 1;
		QList<int> lengths;
		QList<QString> lengthExprs;   // 维数的原始表达式（用于常数求值）
		QList<qlonglong> defaultInt;
		QList<QString> defaultStr;
		bool global = false;
		bool save = false;
		bool isStatic = true;
		bool isPrivate = true;
		bool charaData = false;
		bool isConst = false;

		// 解析主入口
		static UserDefinedVariableData create(QString streamContent, bool isDims, bool isPrivate, const ScriptPosition& pos);
};

#endif // USER_DEFINED_VARIABLE_DATA_H