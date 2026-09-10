#include "variable_storage.h"

VariableStorage::VariableStorage(QObject *parent)
		: QObject(parent)
{
}

void VariableStorage::initialize(int maxCharacters, int localSize)
{
		localIntVars.resize(localSize, 0);
		localStrVars.resize(localSize, "");
}

// ================= 全局整型变量 =================
void VariableStorage::setGlobalInt(const QString &name, int index, qint64 value)
{
		std::string key = name.toStdString();
		if (globalIntVars[key].size() <= static_cast<size_t>(index)) {
				globalIntVars[key].resize(index + 1, 0);
		}
		globalIntVars[key][index] = value;
}

qint64 VariableStorage::getGlobalInt(const QString &name, int index) const
{
		std::string key = name.toStdString();
		auto it = globalIntVars.find(key);
		if (it != globalIntVars.end() && index >= 0 && static_cast<size_t>(index) < it->second.size()) {
				return it->second[index];
		}
		return 0;
}

// ================= 全局字符串变量 =================
void VariableStorage::setGlobalStr(const QString &name, int index, const QString &value)
{
		std::string key = name.toStdString();
		if (globalStrVars[key].size() <= static_cast<size_t>(index)) {
				globalStrVars[key].resize(index + 1, "");
		}
		globalStrVars[key][index] = value.toStdString();
}

QString VariableStorage::getGlobalStr(const QString &name, int index) const
{
		std::string key = name.toStdString();
		auto it = globalStrVars.find(key);
		if (it != globalStrVars.end() && index >= 0 && static_cast<size_t>(index) < it->second.size()) {
				return QString::fromStdString(it->second[index]);
		}
		return "";
}

// ================= 角色整型变量 =================
void VariableStorage::setCharaInt(const QString &name, int charaId, int index, qint64 value)
{
		std::string key = name.toStdString();
		if (charaIntVars[key].size() <= static_cast<size_t>(charaId)) {
				charaIntVars[key].resize(charaId + 1);
		}
		if (charaIntVars[key][charaId].size() <= static_cast<size_t>(index)) {
				charaIntVars[key][charaId].resize(index + 1, 0);
		}
		charaIntVars[key][charaId][index] = value;
}

qint64 VariableStorage::getCharaInt(const QString &name, int charaId, int index) const
{
		std::string key = name.toStdString();
		auto it = charaIntVars.find(key);
		if (it != charaIntVars.end() && charaId >= 0 && static_cast<size_t>(charaId) < it->second.size() && index >= 0 && static_cast<size_t>(index) < it->second[charaId].size()) {
				return it->second[charaId][index];
		}
		return 0;
}

// ================= 角色字符串变量 =================
void VariableStorage::setCharaStr(const QString &name, int charaId, int index, const QString &value)
{
		std::string key = name.toStdString();
		if (charaStrVars[key].size() <= static_cast<size_t>(charaId)) {
				charaStrVars[key].resize(charaId + 1);
		}
		if (charaStrVars[key][charaId].size() <= static_cast<size_t>(index)) {
				charaStrVars[key][charaId].resize(index + 1, "");
		}
		charaStrVars[key][charaId][index] = value.toStdString();
}

QString VariableStorage::getCharaStr(const QString &name, int charaId, int index) const
{
		std::string key = name.toStdString();
		auto it = charaStrVars.find(key);
		if (it != charaStrVars.end() && charaId >= 0 && static_cast<size_t>(charaId) < it->second.size() && index >= 0 && static_cast<size_t>(index) < it->second[charaId].size()) {
				return QString::fromStdString(it->second[charaId][index]);
		}
		return "";
}

// ================= 本地变量 =================
void VariableStorage::setLocalInt(int index, qint64 value)
{
		if (index >= 0 && static_cast<size_t>(index) < localIntVars.size()) {
				localIntVars[index] = value;
		}
}

qint64 VariableStorage::getLocalInt(int index) const
{
		if (index >= 0 && static_cast<size_t>(index) < localIntVars.size()) {
				return localIntVars[index];
		}
		return 0;
}

void VariableStorage::setLocalStr(int index, const QString &value)
{
		if (index >= 0 && static_cast<size_t>(index) < localStrVars.size()) {
				localStrVars[index] = value.toStdString();
		}
}

QString VariableStorage::getLocalStr(int index) const
{
		if (index >= 0 && static_cast<size_t>(index) < localStrVars.size()) {
				return QString::fromStdString(localStrVars[index]);
		}
		return "";
}