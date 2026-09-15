#ifndef USER_DEFINED_VARIABLE_DATA_H
#define USER_DEFINED_VARIABLE_DATA_H

#include <QString>
#include <QList>
#include <QVariant>
#include "script_line.h" // 假设已存在脚本位置结构体

struct UserDefinedVariableData {
		QString name;
		bool typeIsStr = false;
		bool reference = false;
		int dimension = 1;
		QList<int> lengths;
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