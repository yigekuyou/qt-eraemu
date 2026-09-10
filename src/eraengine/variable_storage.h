#ifndef VARIABLE_STORAGE_H
#define VARIABLE_STORAGE_H

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <QObject>
#include <QString>
#include <qqmlregistration.h>

class VariableStorage : public QObject
{
		Q_OBJECT
		QML_ELEMENT

private:
		std::unordered_map<std::string, std::vector<int64_t>> globalIntVars;
		std::unordered_map<std::string, std::vector<std::string>> globalStrVars;
		std::unordered_map<std::string, std::vector<std::vector<int64_t>>> charaIntVars;
		std::unordered_map<std::string, std::vector<std::vector<std::string>>> charaStrVars;
		std::vector<int64_t> localIntVars;
		std::vector<std::string> localStrVars;

public:
		explicit VariableStorage(QObject *parent = nullptr);

		Q_INVOKABLE void initialize(int maxCharacters, int localSize);

		// ================= 全局整型变量 =================
		Q_INVOKABLE void setGlobalInt(const QString &name, int index, qint64 value);
		Q_INVOKABLE qint64 getGlobalInt(const QString &name, int index) const;

		// ================= 全局字符串变量 =================
		Q_INVOKABLE void setGlobalStr(const QString &name, int index, const QString &value);
		Q_INVOKABLE QString getGlobalStr(const QString &name, int index) const;

		// ================= 角色整型变量 =================
		Q_INVOKABLE void setCharaInt(const QString &name, int charaId, int index, qint64 value);
		Q_INVOKABLE qint64 getCharaInt(const QString &name, int charaId, int index) const;

		// ================= 角色字符串变量 =================
		Q_INVOKABLE void setCharaStr(const QString &name, int charaId, int index, const QString &value);
		Q_INVOKABLE QString getCharaStr(const QString &name, int charaId, int index) const;

		// ================= 本地变量 =================
		Q_INVOKABLE void setLocalInt(int index, qint64 value);
		Q_INVOKABLE qint64 getLocalInt(int index) const;
		Q_INVOKABLE void setLocalStr(int index, const QString &value);
		Q_INVOKABLE QString getLocalStr(int index) const;
};

#endif // VARIABLE_STORAGE_H