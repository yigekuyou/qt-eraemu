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
#include "variable_storage.h"
#include "expression_evaluator.h"

VariableStorage::VariableStorage(QObject *parent)
		: QObject(parent)
{
		// Initialize system variable containers
		m_day.fill(0, 1000);
		m_money.fill(0, 1000);
		m_item.fill(0, 1000);
		m_itemsales.fill(0, 1000);
		m_noitem.fill(0, 1000);
		m_bought.fill(0, 1000);
		m_pband.fill(0, 1000);
		m_flag.fill(0, 10000);
		m_tflag.fill(0, 1000);
		m_target.fill(0, 1000);
		m_master.fill(0, 1000);
		m_player.fill(0, 1000);
		m_assi.fill(0, 1000);
		m_assiplay.fill(0, 1000);
		m_up.fill(0, 1000);
		m_down.fill(0, 1000);
		m_losebase.fill(0, 1000);
		m_palamlv.fill(0, 1000);
		m_explv.fill(0, 1000);
		m_ejac.fill(0, 1000);
		m_prevcom.fill(0, 1000);
		m_selectcom.fill(0, 1000);
		m_nextcom.fill(0, 1000);
		m_result.fill(0, 1500);
		m_count.fill(0, 1000);
		m_a.fill(0, 1000);
		m_b.fill(0, 1000);
		m_c.fill(0, 1000);

		// 变量尺寸表在 EraEngine::loadConstantData() 中从 CSV 目录读入
		// （对齐 C#：读取 <CsvDir>/VariableSize.CSV）

		// Register system variable types
		for (int i = 0; i < SYSTEM_VARIABLES.size(); ++i) {
				const auto &entry = SYSTEM_VARIABLES.at(i);
				m_variableTypes[entry.name] = entry.info;
				m_variableIdentifiers[entry.name] = VariableIdentifier(entry.name, entry.info);
		}
}

// 从 VariableSize.csv 读取系统数组尺寸并应用
// （对齐 C# GameData/Variable/VariableData.cs 的 processSystemVariableSize）
bool VariableStorage::loadVariableSizes(const QString& csvPath)
{
		if (!m_variableConfig.loadFromCSV(csvPath)) {
				return false;
		}
		// 名字 -> 容器；只处理有独立容器的系统变量
		const auto resize1D = [this](const QString& name, QList<qint64>& vec) {
				const int n = m_variableConfig.getSize1D(name);
				if (n > 0) vec.resize(n);
		};
		resize1D(QStringLiteral("DAY"), m_day);
		resize1D(QStringLiteral("MONEY"), m_money);
		resize1D(QStringLiteral("ITEM"), m_item);
		resize1D(QStringLiteral("ITEMSALES"), m_itemsales);
		resize1D(QStringLiteral("NOITEM"), m_noitem);
		resize1D(QStringLiteral("BOUGHT"), m_bought);
		resize1D(QStringLiteral("PBAND"), m_pband);
		resize1D(QStringLiteral("FLAG"), m_flag);
		resize1D(QStringLiteral("TFLAG"), m_tflag);
		resize1D(QStringLiteral("TARGET"), m_target);
		resize1D(QStringLiteral("MASTER"), m_master);
		resize1D(QStringLiteral("PLAYER"), m_player);
		resize1D(QStringLiteral("ASSI"), m_assi);
		resize1D(QStringLiteral("ASSIPLAY"), m_assiplay);
		resize1D(QStringLiteral("UP"), m_up);
		resize1D(QStringLiteral("DOWN"), m_down);
		resize1D(QStringLiteral("LOSEBASE"), m_losebase);
		resize1D(QStringLiteral("PALAMLV"), m_palamlv);
		resize1D(QStringLiteral("EXPLV"), m_explv);
		resize1D(QStringLiteral("EJAC"), m_ejac);
		resize1D(QStringLiteral("PREVCOM"), m_prevcom);
		resize1D(QStringLiteral("SELECTCOM"), m_selectcom);
		resize1D(QStringLiteral("NEXTCOM"), m_nextcom);
		resize1D(QStringLiteral("RESULT"), m_result);
		resize1D(QStringLiteral("COUNT"), m_count);
		resize1D(QStringLiteral("A"), m_a);
		resize1D(QStringLiteral("B"), m_b);
		resize1D(QStringLiteral("C"), m_c);
		return true;
}

void VariableStorage::initialize(int maxCharacters, int localSize)
{
		Q_UNUSED(maxCharacters)
		if (localSize > 0) {
				m_localIntVars.fill(0, localSize);
				m_localStrVars.fill(QString(), localSize);
		}
}

// ================= 全局整型 3D 实现示例 =================
void VariableStorage::setGlobalInt3D(const QString &name, int x, int y, int z, qint64 val)
{
		if (x < 0 || y < 0 || z < 0) return;

		auto &grid3D = m_globalInt3D[name];
		if (grid3D.size() <= x) grid3D.resize(x + 1);

		auto &grid2D = grid3D[x];
		if (grid2D.size() <= y) grid2D.resize(y + 1);

		auto &vec = grid2D[y];
		if (vec.size() <= z) vec.resize(z + 1, 0);

		vec[z] = val;
}

qint64 VariableStorage::getGlobalInt3D(const QString &name, int x, int y, int z) const
{
		if (x < 0 || y < 0 || z < 0) return 0;

		auto it = m_globalInt3D.constFind(name);
		if (it != m_globalInt3D.constEnd() && x < it.value().size()) {
				const auto &grid2D = it.value().at(x);
				if (y < grid2D.size()) {
						const auto &vec = grid2D.at(y);
						if (z < vec.size()) {
								return vec.at(z);
						}
				}
		}
		return 0;
}

// ================= 角色整型变量 =================
void VariableStorage::setCharaInt(const QString &name, int charaId, int index, qint64 value)
{
		if (charaId < 0 || index < 0) return;

		auto &charaList = m_charaIntVars[name];
		if (charaList.size() <= charaId) {
				charaList.resize(charaId + 1);
		}
		auto &vec = charaList[charaId];
		if (vec.size() <= index) {
				vec.resize(index + 1, 0);
		}
		vec[index] = value;
}

qint64 VariableStorage::getCharaInt(const QString &name, int charaId, int index) const
{
		if (charaId < 0 || index < 0) return 0;

		auto it = m_charaIntVars.constFind(name);
		if (it != m_charaIntVars.constEnd() && charaId < it.value().size()) {
				const auto &vec = it.value().at(charaId);
				if (index < vec.size()) {
						return vec.at(index);
				}
		}
		return 0;
}

// ================= 本地变量实现 =================
void VariableStorage::setLocalInt(int index, qint64 value)
{
		if (index < 0) {
				return;
		}
		if (index >= m_localIntVars.size()) {
				m_localIntVars.resize(index + 1);
				m_localStrVars.resize(index + 1);
		}
		m_localIntVars[index] = value;
}

qint64 VariableStorage::getLocalInt(int index) const
{
		if (index >= 0 && index < m_localIntVars.size()) {
				return m_localIntVars.at(index);
		}
		return 0;
}

void VariableStorage::setLocalStr(int index, const QString &value)
{
		if (index < 0) {
				return;
		}
		if (index >= m_localStrVars.size()) {
				m_localIntVars.resize(index + 1);
				m_localStrVars.resize(index + 1);
		}
		m_localStrVars[index] = value;
}

QString VariableStorage::getLocalStr(int index) const
{
		if (index >= 0 && index < m_localStrVars.size()) {
				return m_localStrVars.at(index);
		}
		return QString();
}

void VariableStorage::setLocalAlias(const QString &name, int index)
{
		if (!name.isEmpty() && index >= 0) {
				m_localAliases.insert(name.toUpper(), index);
		}
}

int VariableStorage::localAliasIndex(const QString &name) const
{
		return m_localAliases.value(name.toUpper(), -1);
}

// ================= System variable implementations =================
void VariableStorage::setDay(int index, qint64 value) { if (index >= 0 && index < m_day.size()) m_day[index] = value; }
qint64 VariableStorage::getDay(int index) const { return (index >= 0 && index < m_day.size()) ? m_day.at(index) : 0; }

void VariableStorage::setMoney(int index, qint64 value) { if (index >= 0 && index < m_money.size()) m_money[index] = value; }
qint64 VariableStorage::getMoney(int index) const { return (index >= 0 && index < m_money.size()) ? m_money.at(index) : 0; }

void VariableStorage::setItem(int index, qint64 value) { if (index >= 0 && index < m_item.size()) m_item[index] = value; }
qint64 VariableStorage::getItem(int index) const { return (index >= 0 && index < m_item.size()) ? m_item.at(index) : 0; }

void VariableStorage::setItemsales(int index, qint64 value) { if (index >= 0 && index < m_itemsales.size()) m_itemsales[index] = value; }
qint64 VariableStorage::getItemsales(int index) const { return (index >= 0 && index < m_itemsales.size()) ? m_itemsales.at(index) : 0; }

void VariableStorage::setNoitem(int index, qint64 value) { if (index >= 0 && index < m_noitem.size()) m_noitem[index] = value; }
qint64 VariableStorage::getNoitem(int index) const { return (index >= 0 && index < m_noitem.size()) ? m_noitem.at(index) : 0; }

void VariableStorage::setBought(int index, qint64 value) { if (index >= 0 && index < m_bought.size()) m_bought[index] = value; }
qint64 VariableStorage::getBought(int index) const { return (index >= 0 && index < m_bought.size()) ? m_bought.at(index) : 0; }

void VariableStorage::setPband(int index, qint64 value) { if (index >= 0 && index < m_pband.size()) m_pband[index] = value; }
qint64 VariableStorage::getPband(int index) const { return (index >= 0 && index < m_pband.size()) ? m_pband.at(index) : 0; }

void VariableStorage::setFlag(int index, qint64 value) { if (index >= 0 && index < m_flag.size()) m_flag[index] = value; }
qint64 VariableStorage::getFlag(int index) const { return (index >= 0 && index < m_flag.size()) ? m_flag.at(index) : 0; }

void VariableStorage::setTflag(int index, qint64 value) { if (index >= 0 && index < m_tflag.size()) m_tflag[index] = value; }
qint64 VariableStorage::getTflag(int index) const { return (index >= 0 && index < m_tflag.size()) ? m_tflag.at(index) : 0; }

void VariableStorage::setTarget(int index, qint64 value) { if (index >= 0 && index < m_target.size()) m_target[index] = value; }
qint64 VariableStorage::getTarget(int index) const { return (index >= 0 && index < m_target.size()) ? m_target.at(index) : 0; }

void VariableStorage::setMaster(int index, qint64 value) { if (index >= 0 && index < m_master.size()) m_master[index] = value; }
qint64 VariableStorage::getMaster(int index) const { return (index >= 0 && index < m_master.size()) ? m_master.at(index) : 0; }

void VariableStorage::setPlayer(int index, qint64 value) { if (index >= 0 && index < m_player.size()) m_player[index] = value; }
qint64 VariableStorage::getPlayer(int index) const { return (index >= 0 && index < m_player.size()) ? m_player.at(index) : 0; }

void VariableStorage::setAssi(int index, qint64 value) { if (index >= 0 && index < m_assi.size()) m_assi[index] = value; }
qint64 VariableStorage::getAssi(int index) const { return (index >= 0 && index < m_assi.size()) ? m_assi.at(index) : 0; }

void VariableStorage::setAssiplay(int index, qint64 value) { if (index >= 0 && index < m_assiplay.size()) m_assiplay[index] = value; }
qint64 VariableStorage::getAssiplay(int index) const { return (index >= 0 && index < m_assiplay.size()) ? m_assiplay.at(index) : 0; }

void VariableStorage::setUp(int index, qint64 value) { if (index >= 0 && index < m_up.size()) m_up[index] = value; }
qint64 VariableStorage::getUp(int index) const { return (index >= 0 && index < m_up.size()) ? m_up.at(index) : 0; }

void VariableStorage::setDown(int index, qint64 value) { if (index >= 0 && index < m_down.size()) m_down[index] = value; }
qint64 VariableStorage::getDown(int index) const { return (index >= 0 && index < m_down.size()) ? m_down.at(index) : 0; }

void VariableStorage::setLosebase(int index, qint64 value) { if (index >= 0 && index < m_losebase.size()) m_losebase[index] = value; }
qint64 VariableStorage::getLosebase(int index) const { return (index >= 0 && index < m_losebase.size()) ? m_losebase.at(index) : 0; }

void VariableStorage::setPalamlv(int index, qint64 value) { if (index >= 0 && index < m_palamlv.size()) m_palamlv[index] = value; }
qint64 VariableStorage::getPalamlv(int index) const { return (index >= 0 && index < m_palamlv.size()) ? m_palamlv.at(index) : 0; }

void VariableStorage::setExplv(int index, qint64 value) { if (index >= 0 && index < m_explv.size()) m_explv[index] = value; }
qint64 VariableStorage::getExplv(int index) const { return (index >= 0 && index < m_explv.size()) ? m_explv.at(index) : 0; }

void VariableStorage::setEjac(int index, qint64 value) { if (index >= 0 && index < m_ejac.size()) m_ejac[index] = value; }
qint64 VariableStorage::getEjac(int index) const { return (index >= 0 && index < m_ejac.size()) ? m_ejac.at(index) : 0; }

void VariableStorage::setPrevcom(int index, qint64 value) { if (index >= 0 && index < m_prevcom.size()) m_prevcom[index] = value; }
qint64 VariableStorage::getPrevcom(int index) const { return (index >= 0 && index < m_prevcom.size()) ? m_prevcom.at(index) : 0; }

void VariableStorage::setSelectcom(int index, qint64 value) { if (index >= 0 && index < m_selectcom.size()) m_selectcom[index] = value; }
qint64 VariableStorage::getSelectcom(int index) const { return (index >= 0 && index < m_selectcom.size()) ? m_selectcom.at(index) : 0; }

void VariableStorage::setNextcom(int index, qint64 value) { if (index >= 0 && index < m_nextcom.size()) m_nextcom[index] = value; }
qint64 VariableStorage::getNextcom(int index) const { return (index >= 0 && index < m_nextcom.size()) ? m_nextcom.at(index) : 0; }

void VariableStorage::setResult(int index, qint64 value) { if (index >= 0 && index < m_result.size()) m_result[index] = value; }
qint64 VariableStorage::getResult(int index) const { return (index >= 0 && index < m_result.size()) ? m_result.at(index) : 0; }

void VariableStorage::setCount(int index, qint64 value) { if (index >= 0 && index < m_count.size()) m_count[index] = value; }
qint64 VariableStorage::getCount(int index) const { return (index >= 0 && index < m_count.size()) ? m_count.at(index) : 0; }

void VariableStorage::setA(int index, qint64 value) { if (index >= 0 && index < m_a.size()) m_a[index] = value; }
qint64 VariableStorage::getA(int index) const { return (index >= 0 && index < m_a.size()) ? m_a.at(index) : 0; }

void VariableStorage::setB(int index, qint64 value) { if (index >= 0 && index < m_b.size()) m_b[index] = value; }
qint64 VariableStorage::getB(int index) const { return (index >= 0 && index < m_b.size()) ? m_b.at(index) : 0; }

void VariableStorage::setC(int index, qint64 value) { if (index >= 0 && index < m_c.size()) m_c[index] = value; }
qint64 VariableStorage::getC(int index) const { return (index >= 0 && index < m_c.size()) ? m_c.at(index) : 0; }

// ================= Type checking implementations =================
bool VariableStorage::isVariableInteger(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(name);
		return (it != m_variableIdentifiers.constEnd()) ? it.value().isInteger() : false;
}

bool VariableStorage::isVariableString(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(name);
		return (it != m_variableIdentifiers.constEnd()) ? it.value().isString() : false;
}

bool VariableStorage::isVariableLocal(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(name);
		return (it != m_variableIdentifiers.constEnd()) ? it.value().isLocal() : false;
}

bool VariableStorage::isVariableGlobal(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(name);
		return (it != m_variableIdentifiers.constEnd()) ? it.value().isGlobal() : false;
}

bool VariableStorage::isVariableCharacterData(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(name);
		return (it != m_variableIdentifiers.constEnd()) ? it.value().isCharacterData() : false;
}

bool VariableStorage::isVariable1D(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(name);
		return (it != m_variableIdentifiers.constEnd()) ? it.value().is1D() : false;
}

bool VariableStorage::isVariable2D(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(name);
		return (it != m_variableIdentifiers.constEnd()) ? it.value().is2D() : false;
}

bool VariableStorage::isVariable3D(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(name);
		return (it != m_variableIdentifiers.constEnd()) ? it.value().is3D() : false;
}

// ================= Character variable type checking implementations =================
bool VariableStorage::isCharaVariableInteger(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(name);
		return (it != m_variableIdentifiers.constEnd()) ? (it.value().isInteger() && it.value().isCharacterData()) : false;
}

bool VariableStorage::isCharaVariableString(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(name);
		return (it != m_variableIdentifiers.constEnd()) ? (it.value().isString() && it.value().isCharacterData()) : false;
}

bool VariableStorage::isCharaVariable1D(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(name);
		return (it != m_variableIdentifiers.constEnd()) ? (it.value().is1D() && it.value().isCharacterData()) : false;
}

// ================= 全局整型 1D/2D 实现 =================
void VariableStorage::setGlobalInt1D(const QString &name, int x, qint64 val)
{
		if (x < 0) return;
		auto &vec = m_globalInt1D[name];
		if (vec.size() <= x) vec.resize(x + 1, 0);
		vec[x] = val;
}

qint64 VariableStorage::getGlobalInt1D(const QString &name, int x) const
{
		if (x < 0) return 0;
		auto it = m_globalInt1D.constFind(name);
		if (it != m_globalInt1D.constEnd() && x < it.value().size()) {
				return it.value().at(x);
		}
		return 0;
}

void VariableStorage::setGlobalInt2D(const QString &name, int x, int y, qint64 val)
{
		if (x < 0 || y < 0) return;
		auto &grid2D = m_globalInt2D[name];
		if (grid2D.size() <= x) grid2D.resize(x + 1);
		auto &vec = grid2D[x];
		if (vec.size() <= y) vec.resize(y + 1, 0);
		vec[y] = val;
}

qint64 VariableStorage::getGlobalInt2D(const QString &name, int x, int y) const
{
		if (x < 0 || y < 0) return 0;
		auto it = m_globalInt2D.constFind(name);
		if (it != m_globalInt2D.constEnd() && x < it.value().size()) {
				const auto &vec = it.value().at(x);
				if (y < vec.size()) {
						return vec.at(y);
				}
		}
		return 0;
}

// ================= 角色整型 3D 实现 =================
void VariableStorage::setCharaInt3D(const QString &name, int charaId, int x, int y, qint64 value)
{
		if (charaId < 0 || x < 0 || y < 0) return;
		auto &charaList = m_charaIntVars3D[name];
		if (charaList.size() <= charaId) charaList.resize(charaId + 1);
		auto &grid2D = charaList[charaId];
		if (grid2D.size() <= x) grid2D.resize(x + 1);
		auto &vec = grid2D[x];
		if (vec.size() <= y) vec.resize(y + 1, 0);
		vec[y] = value;
}

qint64 VariableStorage::getCharaInt3D(const QString &name, int charaId, int x, int y) const
{
		if (charaId < 0 || x < 0 || y < 0) return 0;
		auto it = m_charaIntVars3D.constFind(name);
		if (it != m_charaIntVars3D.constEnd() && charaId < it.value().size()) {
				const auto &grid2D = it.value().at(charaId);
				if (x < grid2D.size()) {
						const auto &vec = grid2D.at(x);
						if (y < vec.size()) {
								return vec.at(y);
						}
				}
		}
		return 0;
}

// ================= Save/Load Methods =================
bool VariableStorage::saveVariables(const QString &filePath) const
{
		// Implementation will be added in Phase 6
		// For now, just return true to indicate it's a placeholder
		return true;
}

bool VariableStorage::loadVariables(const QString &filePath)
{
		// Implementation will be added in Phase 6
		// For now, just return true to indicate it's a placeholder
		return true;
}

// ================= System Variable Access by Name =================
qint64 VariableStorage::getSystemVariable(const QString &name, int index) const
{
		// Check for system variables and call appropriate getter
		if (name == "DAY") return getDay(index);
		if (name == "MONEY") return getMoney(index);
		if (name == "ITEM") return getItem(index);
		if (name == "ITEMSALES") return getItemsales(index);
		if (name == "NOITEM") return getNoitem(index);
		if (name == "BOUGHT") return getBought(index);
		if (name == "PBAND") return getPband(index);
		if (name == "FLAG") return getFlag(index);
		if (name == "TFLAG") return getTflag(index);
		if (name == "TARGET") return getTarget(index);
		if (name == "MASTER") return getMaster(index);
		if (name == "PLAYER") return getPlayer(index);
		if (name == "ASSI") return getAssi(index);
		if (name == "ASSIPLAY") return getAssiplay(index);
		if (name == "UP") return getUp(index);
		if (name == "DOWN") return getDown(index);
		if (name == "LOSEBASE") return getLosebase(index);
		if (name == "PALAMLV") return getPalamlv(index);
		if (name == "EXPLV") return getExplv(index);
		if (name == "EJAC") return getEjac(index);
		if (name == "PREVCOM") return getPrevcom(index);
		if (name == "SELECTCOM") return getSelectcom(index);
		if (name == "NEXTCOM") return getNextcom(index);
		if (name == "RESULT") return getResult(index);
		if (name == "COUNT") return getCount(index);
		if (name == "A") return getA(index);
		if (name == "B") return getB(index);
		if (name == "C") return getC(index);
		
		// Not a system variable, return 0
		return 0;
}

void VariableStorage::setSystemVariable(const QString &name, int index, qint64 value)
{
		// Check for system variables and call appropriate setter
		if (name == "DAY") { setDay(index, value); return; }
		if (name == "MONEY") { setMoney(index, value); return; }
		if (name == "ITEM") { setItem(index, value); return; }
		if (name == "ITEMSALES") { setItemsales(index, value); return; }
		if (name == "NOITEM") { setNoitem(index, value); return; }
		if (name == "BOUGHT") { setBought(index, value); return; }
		if (name == "PBAND") { setPband(index, value); return; }
		if (name == "FLAG") { setFlag(index, value); return; }
		if (name == "TFLAG") { setTflag(index, value); return; }
		if (name == "TARGET") { setTarget(index, value); return; }
		if (name == "MASTER") { setMaster(index, value); return; }
		if (name == "PLAYER") { setPlayer(index, value); return; }
		if (name == "ASSI") { setAssi(index, value); return; }
		if (name == "ASSIPLAY") { setAssiplay(index, value); return; }
		if (name == "UP") { setUp(index, value); return; }
		if (name == "DOWN") { setDown(index, value); return; }
		if (name == "LOSEBASE") { setLosebase(index, value); return; }
		if (name == "PALAMLV") { setPalamlv(index, value); return; }
		if (name == "EXPLV") { setExplv(index, value); return; }
		if (name == "EJAC") { setEjac(index, value); return; }
		if (name == "PREVCOM") { setPrevcom(index, value); return; }
		if (name == "SELECTCOM") { setSelectcom(index, value); return; }
		if (name == "NEXTCOM") { setNextcom(index, value); return; }
		if (name == "RESULT") { setResult(index, value); return; }
		if (name == "COUNT") { setCount(index, value); return; }
		if (name == "A") { setA(index, value); return; }
		if (name == "B") { setB(index, value); return; }
		if (name == "C") { setC(index, value); return; }
}

// ================= Expression Evaluation =================
QVariant VariableStorage::evaluateExpression(const QString &expression)
{
    ExpressionEvaluator evaluator;
    return evaluator.evaluate(expression, this);
}
