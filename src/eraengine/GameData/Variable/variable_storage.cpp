#include "ast/print_template.h"
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
#include <QSet>
#include <QDebug>
#include "eraengine_log.h"
#include "expression_evaluator.h"
#include "ast/system_variables.h"

// PALAMLV/EXPLV 阈值默认表来自 C# ConfigData 的 _Replace.csv 默认值：
//   PALAMLV {0,100,500,3000,10000,30000,60000,100000,150000,250000}
//   EXPLV   {0,1,4,20,50,200}
// GETPALAMLV/GETEXPLV 用第 i+1 项做阈值（C# VariableData.cs 构造时写入）。
// 这两张表是**变量的默认值**：构造期与 RESETDATA（SetDefaultValue）共用，
// ResetData 恢复默认值而不是清零。
static const qint64 kPalamLvDef[] = {0, 100, 500, 3000, 10000, 30000, 60000, 100000, 150000, 250000};
static const qint64 kExpLvDef[] = {0, 1, 4, 20, 50, 200};

VariableStorage::VariableStorage(QObject *parent)
		: QObject(parent)
{
		// Initialize system variable containers
		m_day.fill(0, 1000);
		m_money.fill(0, 1000);
		m_time.fill(0, 1000);
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
		for (size_t i = 0; i < std::size(kPalamLvDef); ++i) m_palamlv[i] = kPalamLvDef[i];
		for (size_t i = 0; i < std::size(kExpLvDef); ++i) m_explv[i] = kExpLvDef[i];
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

		// 内建角色数据变量：`CFLAG:角色:下标` 必须落到角色存储，否则所有元素
		// 会挤在同一槽位而互相覆盖（表现为「变量似乎不可变」）。
		// 数值角色变量 —— 标量（元素维 0）与一维数组（元素维 1）。
		// 对照 C# VariableCode：__CHARACTER_DATA__ 且非 __ARRAY_2D__。
		for (const char *name : {"ISASSI", "NO"}) {
				registerCharaDataVariable(QString::fromLatin1(name), false, 0);
		}
		for (const char *name : {"BASE", "MAXBASE", "ABL", "TALENT", "EXP", "MARK", "PALAM",
		                         "SOURCE", "EX", "CFLAG", "JUEL", "RELATION", "EQUIP", "TEQUIP",
		                         "STAIN", "GOTJUEL", "NOWEX", "DOWNBASE", "CUP", "CDOWN", "TCVAR"}) {
				registerCharaDataVariable(QString::fromLatin1(name), false, 1);
		}
		// 二维角色数组 CDFLAG:角色:x:y
		registerCharaDataVariable(QStringLiteral("CDFLAG"), false, 2);
		// 内建角色字符串变量：标量（NAME/CALLNAME/…）+ 数组（CSTR）
		for (const char *name : {"NAME", "CALLNAME", "NICKNAME", "MASTERNAME"}) {
				registerCharaDataVariable(QString::fromLatin1(name), true, 0);
		}
		registerCharaDataVariable(QStringLiteral("CSTR"), true, 1);
}

// 从 VariableSize.csv 读取系统数组尺寸并应用
// （对齐 C# GameData/Variable/VariableData.cs 的 processSystemVariableSize）
bool VariableStorage::loadVariableSizes(const QString& csvPath)
{
		if (!m_variableConfig.loadFromCSV(csvPath)) {
				qWarning() << "[var] VariableSize 加载失败:" << csvPath;
				return false;
		}
		qDebug() << "[var] VariableSize 加载:" << csvPath;
		// 名字 -> 容器；只处理有独立容器的系统变量
		const auto resize1D = [this](const QString& name, QList<qint64>& vec) {
				const int n = m_variableConfig.getSize1D(name);
				if (n > 0) vec.resize(n);
		};
		resize1D(QStringLiteral("DAY"), m_day);
		resize1D(QStringLiteral("MONEY"), m_money);
		resize1D(QStringLiteral("TIME"), m_time);
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

int VariableStorage::arraySize(const QString& name) const
{
        return arraySizeRaw(storageName(name));
}

int VariableStorage::arraySizeRaw(const QString& key) const
{

		// 局部/实参槽不在 m_global* 容器里，单独报尺寸（VARSIZE("LOCAL")）
		if (key == QLatin1String("LOCAL")) return m_localIntVars.size();
		if (key == QLatin1String("LOCALS")) return m_localStrVars.size();
		if (key == QLatin1String("ARG")) return m_argIntVars.size();
		if (key == QLatin1String("ARGS")) return m_argStrVars.size();
		if (auto it = m_globalStr1D.constFind(key); it != m_globalStr1D.constEnd()) return it->size();
		if (auto it = m_globalInt1D.constFind(key); it != m_globalInt1D.constEnd()) return it->size();
		if (auto it = m_globalStr2D.constFind(key); it != m_globalStr2D.constEnd()) return it->size();
		if (auto it = m_globalInt2D.constFind(key); it != m_globalInt2D.constEnd()) return it->size();
		return 0;
}

QList<int> VariableStorage::arrayLengths(const QString& name) const
{
    const QString key = resolvedStorageName(name);
    const auto shape2 = [](const auto& values) -> QList<int> {
        int columns = 0;
        for (const auto& row : values) columns = qMax(columns, int(row.size()));
        return {int(values.size()), columns};
    };
    const auto shape3 = [](const auto& values) -> QList<int> {
        int rows = 0, columns = 0;
        for (const auto& plane : values) {
            rows = qMax(rows, int(plane.size()));
            for (const auto& row : plane) columns = qMax(columns, int(row.size()));
        }
        return {int(values.size()), rows, columns};
    };
    if (auto it = m_globalInt3D.constFind(key); it != m_globalInt3D.constEnd()) return shape3(*it);
    if (auto it = m_globalStr3D.constFind(key); it != m_globalStr3D.constEnd()) return shape3(*it);
    if (auto it = m_globalInt2D.constFind(key); it != m_globalInt2D.constEnd()) return shape2(*it);
    if (auto it = m_globalStr2D.constFind(key); it != m_globalStr2D.constEnd()) return shape2(*it);
    const auto sizes3 = m_variableConfig.getSize3D(key);
    if (!sizes3.isEmpty()) return sizes3;
    const auto sizes2 = m_variableConfig.getSize2D(key);
    if (sizes2.first > 0 || sizes2.second > 0) return {sizes2.first, sizes2.second};
    const int stored = arraySizeRaw(key);
    return {stored > 0 ? stored : m_variableConfig.getSize1D(key)};
}

void VariableStorage::ensureArraySize(const QString& name, int size, bool stringArray)
{
		if (size <= 0) return;
		if (stringArray) {
			auto& values = m_globalStr1D[storageName(name)];
			if (values.size() < size) values.resize(size);
		} else {
			auto& values = m_globalInt1D[storageName(name)];
			if (values.size() < size) values.resize(size);
		}
}

void VariableStorage::initialize(int maxCharacters, int localSize)
{
		Q_UNUSED(maxCharacters)
		qDebug() << "[var] initialize localSize =" << localSize;
		m_privateScopeCache.clear();   // 脚本（重）装载后私有作用域可能变化
		if (localSize > 0) {
				m_localIntVars.fill(0, localSize);
				m_localStrVars.fill(FlatStringValue{}, localSize);
		}
}

// ================= 全局整型 3D 实现示例 =================
void VariableStorage::setGlobalInt3D(const QString &name, int x, int y, int z, qint64 val)
{
		if (x < 0 || y < 0 || z < 0) return;

		auto &grid3D = m_globalInt3D[storageName(name)];
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

		auto it = m_globalInt3D.constFind(storageName(name));
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
// Raw：按**运行时下标**直接存取（不做 运行时->模板 翻译），供角色列表自身使用。
void VariableStorage::setCharaIntRaw(const QString &name, int charaId, int index, qint64 value)
{
		if (charaId < 0 || index < 0) return;
		auto &charaList = m_charaIntVars[storageName(name)];
		if (charaList.size() <= charaId) {
				charaList.resize(charaId + 1);
		}
		auto &vec = charaList[charaId];
		if (vec.size() <= index) {
				vec.resize(index + 1, 0);
		}
		vec[index] = value;
}

void VariableStorage::setCharaInt(const QString &name, int charaId, int index, qint64 value)
{
		if (charaId < 0 || index < 0) return;
		setCharaIntRaw(name, resolveCharaIndex(charaId), index, value);
}

// ---------------------------------------------------------------------------
// 训练流程变量重置（对齐 C# GameData/Variable/VariableEvaluator.cs
//   UpdateAfterShowUsercom / UpdateAfterInputCom）
// ---------------------------------------------------------------------------
void VariableStorage::updateAfterShowUsercom()
{
		// UP = 0, DOWN = 0, LOSEBASE = 0（全局 1D）
		m_up.fill(0);
		m_down.fill(0);
		m_losebase.fill(0);
		// 各角色 DOWNBASE = 0, CUP = 0, CDOWN = 0
		static const char* const kCharaKeys[] = {"DOWNBASE", "CUP", "CDOWN"};
		for (const char* k : kCharaKeys) {
				auto it = m_charaIntVars.find(storageName(QString::fromLatin1(k)));
				if (it == m_charaIntVars.end()) continue;
				for (QList<qint64>& vec : it.value()) vec.fill(0);
		}
}

void VariableStorage::updateAfterInputCom()
{
		// 各角色 NOWEX = 0（对齐 C#：选择中以外的角色也全部重置）
		auto it = m_charaIntVars.find(storageName(QStringLiteral("NOWEX")));
		if (it == m_charaIntVars.end()) return;
		for (QList<qint64>& vec : it.value()) vec.fill(0);
}

void VariableStorage::updateInBeginTrain()
{
		// 对齐 C# GameData/Variable/VariableEvaluator.cs UpdateInBeginTrain()：
		// BEGIN TRAIN 的入口（@EVENTTRAIN 之前）把「上一轮调教」遗留的变量复位。
		//
		// 关键：NEXTCOM = -1。系统层 endCallEventTrain() 见到 NEXTCOM >= 0 就会
		// 「不显示行动菜单、直接执行 NEXTCOM 指定的调教指令」（C# 的
		// CALLTRAIN/連続実行 机制；C# 同一处还会把 NEXTCOM 写回 0，靠 ERB 自己
		// 改值避免死循环 —— 参 Process.SystemProc.cs endCallEventTrain）。
		// 本移植此前完全没有接这个复位，容器初值又是 0，于是执行到
		// BEGIN TRAIN 时 NEXTCOM == 0 >= 0 恒成立 —— 起床后必然「跳」进
		// COM0（eraTW 的 [0] 愛撫），行动菜单被整段跳过。
		setAssiplay(0, 0);
		setPrevcom(0, -1);
		setNextcom(0, -1);

		// TFLAG 全部 0、TSTR 全部 ""（C# 同处逐一 fill）
		m_tflag.fill(0);
		const int tstrSize = m_variableConfig.getSize1D(QStringLiteral("TSTR"));
		for (int i = 0; i < tstrSize; ++i) {
				setGlobalStr1D(QStringLiteral("TSTR"), i, QString());
		}

		// 全角色数值归零（C#：GOTJUEL/TEQUIP/EX/STAIN/PALAM/SOURCE/TCVAR）。
		// 与 updateAfterShowUsercom 同款：直接遍历角色存储的每个角色槽。
		static const char* const kCharaKeys[] = {
			"GOTJUEL", "TEQUIP", "EX", "PALAM", "SOURCE", "TCVAR",
		};
		for (const char* k : kCharaKeys) {
				auto it = m_charaIntVars.find(storageName(QString::fromLatin1(k)));
				if (it == m_charaIntVars.end()) continue;
				for (QList<qint64>& vec : it.value()) vec.fill(0);
		}
		// STAIN 走 C# 的 setDefaultStain()：_REPLACE.CSV 初值未装载时按 0
		// （与 RESET_STAIN 内建指令取同一语义）。
		auto stain = m_charaIntVars.find(storageName(QStringLiteral("STAIN")));
		if (stain != m_charaIntVars.end()) {
				for (QList<qint64>& vec : stain.value()) vec.fill(0);
		}
}

qint64 VariableStorage::getCharaInt(const QString &name, int charaId, int index) const
{
		if (charaId < 0 || index < 0) return 0;
		charaId = resolveCharaIndex(charaId);
		if (charaId < 0) return 0;   // VOID 角色（ADDVOIDCHARA）：无模板数据

		auto it = m_charaIntVars.constFind(storageName(name));
		if (it != m_charaIntVars.constEnd() && charaId < it.value().size()) {
				const auto &vec = it.value().at(charaId);
				if (index < vec.size()) {
						return vec.at(index);
				}
		}
		return 0;
}

// ================= 角色字符串变量 =================
void VariableStorage::setCharaStr(const QString &name, int charaId, int index, const QString &value)
{
		if (charaId < 0 || index < 0) return;
		setCharaStrRaw(name, resolveCharaIndex(charaId), index, value);
}

void VariableStorage::setCharaStrRaw(const QString &name, int charaId, int index, const QString &value)
{
		if (charaId < 0 || index < 0) return;
		auto &charaList = m_charaStrVars[storageName(name)];
		if (charaList.size() <= charaId) charaList.resize(charaId + 1);
		auto &vec = charaList[charaId];
		if (vec.size() <= index) vec.resize(index + 1);
		vec[index] = flatString(QVariant(value));
}

QString VariableStorage::getCharaStr(const QString &name, int charaId, int index) const
{
		if (charaId < 0 || index < 0) return QString();
		charaId = resolveCharaIndex(charaId);
		if (charaId < 0) return QString();   // VOID 角色：无模板数据
		auto it = m_charaStrVars.constFind(storageName(name));
		if (it != m_charaStrVars.constEnd() && charaId < it.value().size()) {
				const auto &vec = it.value().at(charaId);
				if (index < vec.size()) return vec.at(index).text;
		}
		return QString();
}

// ================= 角色列表（ADDCHARA / DELCHARA / CHARANUM）=================
// 对齐 C# VariableEvaluator.AddChara / DelChara：ADDCHARA 把 CSV 模板登记为
// 一个运行时角色（数据本身按模板号存放，见 resolveCharaIndex）。
void VariableStorage::addChara(int csvNo)
{
		if (csvNo < 0) return;
		m_charaList.append(csvNo);
		const int index = m_charaList.size() - 1;
		// NO:i = 该运行时角色所用的模板号（C# NO 系统变量）。
		// 必须写**原始运行时下标**（绕过 resolveCharaIndex，否则会落到模板槽）。
		setCharaIntRaw(QStringLiteral("NO"), index, 0, csvNo);
		qDebug() << "[chara] ADDCHARA" << csvNo << "-> index" << index
				 << "CHARANUM" << m_charaList.size();
}

bool VariableStorage::delChara(int index)
{
		if (index < 0 || index >= m_charaList.size()) return false;
		m_charaList.removeAt(index);
		m_charaSp.remove(index);
		qDebug() << "[chara] DELCHARA index" << index << "CHARANUM" << m_charaList.size();
		return true;
}

void VariableStorage::addSpChara(int csvNo)
{
		if (csvNo < 0) return;
		m_charaList.append(csvNo);
		m_charaSp.insert(m_charaList.size() - 1);
		qDebug() << "[chara] ADDSPCHARA" << csvNo << "-> index" << m_charaList.size() - 1;
}

void VariableStorage::swapChara(int a, int b)
{
		if (a < 0 || b < 0 || a >= m_charaList.size() || b >= m_charaList.size() || a == b) return;
		m_charaList.swapItemsAt(a, b);
		const bool sa = m_charaSp.contains(a);
		const bool sb = m_charaSp.contains(b);
		if (sa != sb) {
			if (sa) { m_charaSp.remove(a); m_charaSp.insert(b); }
			else    { m_charaSp.remove(b); m_charaSp.insert(a); }
		}
}

void VariableStorage::copyChara(int dst, int src)
{
		if (dst < 0 || src < 0 || dst >= m_charaList.size() || src >= m_charaList.size()) return;
		m_charaList[dst] = m_charaList[src];
		if (m_charaSp.contains(src)) m_charaSp.insert(dst);
		else m_charaSp.remove(dst);
}

void VariableStorage::addCopyChara(int src)
{
		if (src < 0 || src >= m_charaList.size()) return;
		m_charaList.append(m_charaList.at(src));
		if (m_charaSp.contains(src)) m_charaSp.insert(m_charaList.size() - 1);
}

void VariableStorage::pickupChara(const QList<int>& indexes)
{
		QList<int> newList;
		QSet<int> newSp;
		newList.reserve(indexes.size());
		for (int i : indexes) {
			if (i < 0 || i >= m_charaList.size()) continue;
			newList.append(m_charaList.at(i));
			if (m_charaSp.contains(i)) newSp.insert(newList.size() - 1);
		}
		m_charaList = newList;
		m_charaSp = newSp;
}

VariableStorage::CharacterSnapshot VariableStorage::captureCharacterSnapshot(const QList<int>& indexes) const
{
    CharacterSnapshot result;
    if (indexes.isEmpty()) {
        for (int i = 0; i < m_charaList.size(); ++i)
            result.append({m_charaList.at(i), m_charaSp.contains(i)});
    } else for (int i : indexes) {
        if (i >= 0 && i < m_charaList.size())
            result.append({m_charaList.at(i), m_charaSp.contains(i)});
    }
    return result;
}

bool VariableStorage::decodeCharacterSnapshot(const QString& text, CharacterSnapshot& snapshot)
{
    const auto fields = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (fields.size() % 2) return false;
    CharacterSnapshot next;
    for (int i = 0; i < fields.size(); i += 2) {
        bool okNumber = false, okSpecial = false;
        const int number = fields.at(i).toInt(&okNumber);
        const int special = fields.at(i + 1).toInt(&okSpecial);
        if (!okNumber || number < 0 || !okSpecial || (special != 0 && special != 1)) return false;
        next.append({number, special != 0});
    }
    snapshot = std::move(next);
    return true;
}

QString VariableStorage::serializeCharacterSnapshot(const CharacterSnapshot& snapshot)
{
    QStringList fields;
    for (const auto& entry : snapshot)
        fields << QString::number(entry.number) << (entry.special ? QStringLiteral("1") : QStringLiteral("0"));
    return fields.join(QLatin1Char(' '));
}

void VariableStorage::appendCharacterSnapshot(const CharacterSnapshot& snapshot)
{
    for (const auto& entry : snapshot) {
        m_charaList.append(entry.number);
        if (entry.special) m_charaSp.insert(m_charaList.size() - 1);
    }
}

QString VariableStorage::dumpCharaList() const
{
    return serializeCharacterSnapshot(captureCharacterSnapshot());
}

void VariableStorage::appendCharaList(const QString& text)
{
    CharacterSnapshot snapshot;
    if (decodeCharacterSnapshot(text, snapshot)) appendCharacterSnapshot(snapshot);
}

void VariableStorage::resetGlobals()
{
		// C# ResetGlobalData：只重置 **GLOBAL/GLOBALS 声明的广域变量**。
		// 函数私有变量在这里与广域变量同容器存放，键形如 `函数名\x1f变量名`
		// （见 setPrivateScope）—— 必须保留，否则 RESETGLOBAL 会把各函数的
		// 私有计数器一并清零（表现为「跨函数累积的统计量丢失」）。
		const auto isPrivateKey = [](const QString& k) {
			return k.contains(QChar(0x1f));
		};
		// C# SetDefaultGlobalValue 只重置 GLOBAL/GLOBALS（内建数组）+ 用户 #GLOBAL/#GLOBALS；
		// **内建系统/字符串变量（DAY/FLAG/UP/…/STR/RESULTS…）不在重置之列**。
		// 此前是「非私有键一律 erase」，会把内建字符串数组 STR（Str.csv 的数据）
		// 和 RESULTS 一并清掉 —— eraTW 的 %STR:(…)% 场所名、RETURNF 的 RESULTS 会失效。
		static const QSet<QString> kPreservedBuiltinStrings = {
			QStringLiteral("STR"),      QStringLiteral("RESULTS"), QStringLiteral("SAVESTR"),
			QStringLiteral("TSTR"),     QStringLiteral("CSTR"),    QStringLiteral("SAVEDATA_TEXT"),
		};
		const auto isPreservedBuiltin = [&](const QString& k) {
			const QString up = eraUpperKey(k);
			// CSV 名表（<VAR>NAME：ABLNAME/TALENTNAME/DAYNAME/…）是装载期由 CSV
			// 建立的常量数据，不属于 GLOBAL/GLOBALS，RESETGLOBAL 不应清空
			// （C# SetDefaultGlobalValue 只重置 GLOBAL/GLOBALS + 用户广域变量）。
			if (up.endsWith(QLatin1String("NAME"))) return true;
			return hasSystemVariable(k) || kPreservedBuiltinStrings.contains(up);
		};
		const auto purge = [&](auto& map) {
			for (auto it = map.begin(); it != map.end(); ) {
				if (isPrivateKey(it.key()) || isPreservedBuiltin(it.key())) ++it;
				else it = map.erase(it);
			}
		};
		purge(m_globalInt1D);
		purge(m_globalInt2D);
		purge(m_globalInt3D);
		purge(m_globalStr1D);
		purge(m_globalStr2D);
		purge(m_globalStr3D);
		qDebug() << "[global] RESETGLOBAL（保留函数私有变量）";
}

// ================= RESETDATA / 新开游戏 =================
// 对齐 C# VariableEvaluator.ResetData：SetDefaultValue(全部变量) + CharacterList.Clear()。
// 保留：NAME 表、STR 等 CSV 装载期「默认值」数据、CSV 模板快照（ADDCHARA 重新
// 填充角色时仍要用）；清除：全部运行时数值（内建整型数组、用户广域、角色运行时
// 数据、本地槽）。
void VariableStorage::resetForNewGame()
{
		clearCharaList();
		resetGlobals();   // 用户广域变量回默认（内部保留私有键/NAME/内建字符串）
		const auto zero = [](QList<qint64>& list) { list.fill(0); };
		for (QList<qint64>* p : { &m_day, &m_money, &m_time, &m_item, &m_itemsales,
		     &m_noitem, &m_bought, &m_pband, &m_flag, &m_tflag, &m_target,
		     &m_master, &m_player, &m_assi, &m_assiplay, &m_up, &m_down,
		     &m_losebase, &m_palamlv, &m_explv, &m_ejac, &m_prevcom,
		     &m_selectcom, &m_nextcom, &m_result, &m_count, &m_a, &m_b, &m_c }) {
			zero(*p);
		}
		// PALAMLV/EXPLV 的默认值表非零 —— SetDefaultValue 语义是恢复默认值
		for (size_t i = 0; i < std::size(kPalamLvDef); ++i) m_palamlv[i] = kPalamLvDef[i];
		for (size_t i = 0; i < std::size(kExpLvDef); ++i) m_explv[i] = kExpLvDef[i];
		m_systemIntVars.clear();
		m_systemStrVars.clear();
		// 函数私有静态变量（键形如 `函数名\x1f变量名`，#DIM 静态局部）也要回默认
		// —— 对齐 C# ResetData 的 SetDefaultLocalValue。RESETGLOBAL 保留它们
		//（跨函数累积的统计量），但 ResetData 是「整个游戏回默认」，必须清。
		// 此前不清 -> eraTW 之类重开游戏后静态计数器继承上一周目。
		const auto purgePrivate = [&](auto& map) {
			for (auto it = map.begin(); it != map.end(); ) {
				if (it.key().contains(QChar(0x1f))) it = map.erase(it);
				else ++it;
			}
		};
		purgePrivate(m_globalInt1D);
		purgePrivate(m_globalInt2D);
		purgePrivate(m_globalInt3D);
		purgePrivate(m_globalStr1D);
		purgePrivate(m_globalStr2D);
		purgePrivate(m_globalStr3D);
		m_privateScopeCache.clear();
		// 角色数据回默认值。本移植把 CSV 模板默认值**直接写进角色存储**
		// （m_charaIntVars/m_charaStrVars，按模板号存放），ADDCHARA/LOADCHARA 之后
		// 都从这些容器取 NAME/ABL… 模板默认值 —— 因此这里必须从模板快照恢复，
		// 而不是清空；否则清空后新增/载入的角色 NAME/ABL… 全空（组 9 LOADCHARA
		// NAME、组 28 ABL:技巧 的回归根因）。运行时 2D 角色数组（无 CSV 快照）仍清空。
		m_charaIntVars = m_csvIntVars;
		m_charaIntVars3D = m_csvIntVars3D;
		m_charaStrVars = m_csvStrVars;
		m_charaInt2D.clear();
		m_charaStr2D.clear();
		// 本地槽（LOCAL/ARG/LOCALS/ARGS）与形参
		m_localIntVars.fill(0);
		m_argIntVars.fill(0);
		m_localStrVars.fill(FlatStringValue{});
		m_argStrVars.fill(FlatStringValue{});
		m_parameters.clear();
		qDebug() << "[var] RESETDATA 变量回默认值，角色列表已清空";
}

// ================= CSV 模板快照 =================
// 装载角色 CSV 之后调用一次：把当前角色存储整体复制为「模板」。
// 此后 CSV* 函数读快照，脚本对 VAR:角色:下标 的修改不再影响模板值。
void VariableStorage::snapshotCharaTemplates()
{
		m_csvIntVars = m_charaIntVars;
		m_csvIntVars3D = m_charaIntVars3D;
		m_csvStrVars = m_charaStrVars;
		qDebug() << "[chara] CSV 模板快照：整型变量" << m_csvIntVars.size()
				 << "三维" << m_csvIntVars3D.size()
				 << "字符串" << m_csvStrVars.size();
}

qint64 VariableStorage::csvCharaInt(const QString& name, int charaId, int index) const
{
		if (charaId < 0 || index < 0) return 0;
		const auto it = m_csvIntVars.constFind(storageName(name));
		if (it == m_csvIntVars.constEnd()) return 0;
		if (charaId >= it.value().size()) return 0;
		const auto& vec = it.value().at(charaId);
		return index < vec.size() ? vec.at(index) : 0;
}

QString VariableStorage::csvCharaStr(const QString& name, int charaId, int index) const
{
		if (charaId < 0 || index < 0) return QString();
		const auto it = m_csvStrVars.constFind(storageName(name));
		if (it == m_csvStrVars.constEnd()) return QString();
		if (charaId >= it.value().size()) return QString();
		const auto& vec = it.value().at(charaId);
		return index < vec.size() ? vec.at(index).text : QString();
}

// ================= 角色数据变量（CHARADATA） =================
void VariableStorage::registerCharaDataVariable(const QString &name, bool isString,
                                                int elementDimension)
{
		if (name.isEmpty()) return;
		qDebug() << "[var] registerCharaData" << name << "str =" << isString
				 << "dim =" << elementDimension;
		CharaDataInfo info;
		info.isString = isString;
		info.dimension = qBound(0, elementDimension, 2);
		m_charaDataVars.insert(eraUpperKey(name), info);
}

bool VariableStorage::isCharaDataVariable(const QString &name) const
{
		return m_charaDataVars.contains(eraUpperKey(name));
}

bool VariableStorage::isCharaDataString(const QString &name) const
{
		const auto it = m_charaDataVars.constFind(eraUpperKey(name));
		return it != m_charaDataVars.constEnd() && it.value().isString;
}

int VariableStorage::charaDataDimension(const QString &name) const
{
		const auto it = m_charaDataVars.constFind(eraUpperKey(name));
		return it == m_charaDataVars.constEnd() ? 0 : it.value().dimension;
}

// 实参规约（对齐 C# VariableParser.ReduceVariable）：
//   · 参数足够（>= 1 + 元素维数）→ 第 1 个是角色号，其余是元素下标
//   · 参数不足（省略了角色维）→ 角色号取 TARGET，其余是元素下标
//   · 完全没有参数 → 角色号取 TARGET，元素下标补 0
void VariableStorage::reduceCharaArgs(const QString &name, const QList<int> &indices,
                                      int &charaId, QList<int> &elements) const
{
		const int dim = charaDataDimension(name);
		const int total = dim + 1;
		elements.clear();
		int offset = 0;
		if (indices.size() >= total) {
				charaId = indices.at(0);
				offset = 1;
		} else {
				charaId = static_cast<int>(getSystemVariable(QStringLiteral("TARGET"), 0));
		}
		if (charaId < 0) charaId = 0;
		for (int i = 0; i < dim; ++i) {
				const int src = offset + i;
				elements.append(src < indices.size() ? indices.at(src) : 0);
		}
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

void VariableStorage::ensureLocalSize(int n)
{
		if (n > m_localIntVars.size()) m_localIntVars.resize(n);
		if (n > m_localStrVars.size()) m_localStrVars.resize(n);
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
		m_localStrVars[index] = flatString(QVariant(value));
}

QString VariableStorage::getLocalStr(int index) const
{
		if (index >= 0 && index < m_localStrVars.size()) {
				return m_localStrVars.at(index).text;
		}
		return QString();
}

// ---- 函数实参 ARG / ARGS（与 LOCAL / LOCALS 分离，见头文件说明）----
void VariableStorage::setArgInt(int index, qint64 value)
{
		if (index < 0) return;
		if (index >= m_argIntVars.size()) m_argIntVars.resize(index + 1);
		m_argIntVars[index] = value;
}

qint64 VariableStorage::getArgInt(int index) const
{
		return (index >= 0 && index < m_argIntVars.size()) ? m_argIntVars.at(index) : 0;
}

void VariableStorage::setArgStr(int index, const QString& value)
{
		if (index < 0) return;
		if (index >= m_argStrVars.size()) m_argStrVars.resize(index + 1);
		m_argStrVars[index] = flatString(QVariant(value));
}

QString VariableStorage::getArgStr(int index) const
{
		return (index >= 0 && index < m_argStrVars.size()) ? m_argStrVars.at(index).text : QString();
}

void VariableStorage::setLocalAlias(const QString &name, int index){
		if (!name.isEmpty() && index >= 0) {
				m_localAliases.insert(eraUpperKey(name), index);
		}
}

int VariableStorage::localAliasIndex(const QString &name) const
{
		return m_localAliases.value(eraUpperKey(name), -1);
}

// ================= System variable implementations =================
void VariableStorage::setDay(int index, qint64 value) { if (index >= 0 && index < m_day.size()) m_day[index] = value; }
qint64 VariableStorage::getDay(int index) const { return (index >= 0 && index < m_day.size()) ? m_day.at(index) : 0; }

void VariableStorage::setMoney(int index, qint64 value) { if (index >= 0 && index < m_money.size()) m_money[index] = value; }
qint64 VariableStorage::getMoney(int index) const { return (index >= 0 && index < m_money.size()) ? m_money.at(index) : 0; }

void VariableStorage::setTime(int index, qint64 value) { if (index >= 0 && index < m_time.size()) m_time[index] = value; }
qint64 VariableStorage::getTime(int index) const { return (index >= 0 && index < m_time.size()) ? m_time.at(index) : 0; }

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
		auto it = m_variableIdentifiers.constFind(eraUpperKey(name));
		return (it != m_variableIdentifiers.constEnd()) ? it.value().isInteger() : false;
}

bool VariableStorage::isVariableString(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(eraUpperKey(name));
		return (it != m_variableIdentifiers.constEnd()) ? it.value().isString() : false;
}

bool VariableStorage::isVariableLocal(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(eraUpperKey(name));
		return (it != m_variableIdentifiers.constEnd()) ? it.value().isLocal() : false;
}

bool VariableStorage::isVariableGlobal(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(eraUpperKey(name));
		return (it != m_variableIdentifiers.constEnd()) ? it.value().isGlobal() : false;
}

bool VariableStorage::isVariableCharacterData(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(eraUpperKey(name));
		return (it != m_variableIdentifiers.constEnd()) ? it.value().isCharacterData() : false;
}

bool VariableStorage::isVariable1D(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(eraUpperKey(name));
		return (it != m_variableIdentifiers.constEnd()) ? it.value().is1D() : false;
}

bool VariableStorage::isVariable2D(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(eraUpperKey(name));
		return (it != m_variableIdentifiers.constEnd()) ? it.value().is2D() : false;
}

bool VariableStorage::isVariable3D(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(eraUpperKey(name));
		return (it != m_variableIdentifiers.constEnd()) ? it.value().is3D() : false;
}

// ================= Character variable type checking implementations =================
bool VariableStorage::isCharaVariableInteger(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(eraUpperKey(name));
		return (it != m_variableIdentifiers.constEnd()) ? (it.value().isInteger() && it.value().isCharacterData()) : false;
}

bool VariableStorage::isCharaVariableString(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(eraUpperKey(name));
		return (it != m_variableIdentifiers.constEnd()) ? (it.value().isString() && it.value().isCharacterData()) : false;
}

bool VariableStorage::isCharaVariable1D(const QString &name) const
{
		auto it = m_variableIdentifiers.constFind(eraUpperKey(name));
		return (it != m_variableIdentifiers.constEnd()) ? (it.value().is1D() && it.value().isCharacterData()) : false;
}

// ================= 全局整型 1D/2D 实现 =================
void VariableStorage::setGlobalInt1D(const QString &name, int x, qint64 val)
{
		if (x < 0) return;
		auto &vec = m_globalInt1D[storageName(name)];
		if (vec.size() <= x) vec.resize(x + 1, 0);
		vec[x] = val;
}

void VariableStorage::setGlobalStr1D(const QString &name, int x, const QString &value)
{
		if (x < 0) return;
		auto &vec = m_globalStr1D[storageName(name)];
		if (vec.size() <= x) vec.resize(x + 1);
		vec[x] = flatString(QVariant(value));
}

QString VariableStorage::getGlobalStr1D(const QString &name, int x) const
{
        return getGlobalStr1DRaw(storageName(name), x);
}

QString VariableStorage::getGlobalStr1DRaw(const QString& storageKey, int x) const
{
        if (x < 0) return QString();
        auto it = m_globalStr1D.constFind(storageKey);
        if (it != m_globalStr1D.constEnd() && x < it.value().size()) return it.value().at(x).text;
        return QString();
}

void VariableStorage::setGlobalStr2D(const QString &name, int x, int y, const QString &value)
{
		if (x < 0 || y < 0) return;
		auto &table = m_globalStr2D[storageName(name)];
		if (table.size() <= x) table.resize(x + 1);
		if (table[x].size() <= y) table[x].resize(y + 1);
		table[x][y] = flatString(QVariant(value));
}

QString VariableStorage::getGlobalStr2D(const QString &name, int x, int y) const
{
		if (x < 0 || y < 0) return QString();
		auto it = m_globalStr2D.constFind(storageName(name));
		if (it == m_globalStr2D.constEnd()) return QString();
		if (x >= it.value().size() || y >= it.value().at(x).size()) return QString();
		return it.value().at(x).at(y).text;
}

void VariableStorage::setSystemStr(const QString &name, int index, const QString &value)
{
		if (index != 0) return;   // 系统字符串变量目前只有单值（RESULTS / SAVEDATA_TEXT）
		m_systemStrVars[eraUpperKey(name)] = flatString(QVariant(value));
}

QString VariableStorage::getSystemStr(const QString &name, int index) const
{
		if (index != 0) return QString();
		return m_systemStrVars.value(eraUpperKey(name)).text;
}

qint64 VariableStorage::getGlobalInt1D(const QString &name, int x) const
{
		if (x < 0) return 0;
		auto it = m_globalInt1D.constFind(storageName(name));
		if (it != m_globalInt1D.constEnd() && x < it.value().size()) {
				return it.value().at(x);
		}
		return 0;
}

void VariableStorage::setGlobalInt2D(const QString &name, int x, int y, qint64 val)
{
		if (x < 0 || y < 0) return;
		auto &grid2D = m_globalInt2D[storageName(name)];
		if (grid2D.size() <= x) grid2D.resize(x + 1);
		auto &vec = grid2D[x];
		if (vec.size() <= y) vec.resize(y + 1, 0);
		vec[y] = val;
}

qint64 VariableStorage::getGlobalInt2D(const QString &name, int x, int y) const
{
		if (x < 0 || y < 0) return 0;
		auto it = m_globalInt2D.constFind(storageName(name));
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
		charaId = resolveCharaIndex(charaId);
		auto &charaList = m_charaIntVars3D[storageName(name)];
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
		charaId = resolveCharaIndex(charaId);
		if (charaId < 0) return 0;   // VOID 角色：无模板数据
		auto it = m_charaIntVars3D.constFind(storageName(name));
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
bool VariableStorage::saveVariables(const QString &path) const
{
		qWarning() << "[var] saveVariables 是未实现的占位（Phase 6）:" << path;
		// Implementation will be added in Phase 6
		// For now, just return true to indicate it's a placeholder
		return true;
}

bool VariableStorage::loadVariables(const QString &path)
{
		qWarning() << "[var] loadVariables 是未实现的占位（Phase 6）:" << path;
		// Implementation will be added in Phase 6
		// For now, just return true to indicate it's a placeholder
		return true;
}

// ================= System Variable Access by Name =================
bool VariableStorage::hasSystemVariable(const QString &name) const
{
		static const QSet<QString> kNames = {
QStringLiteral("DAY"),QStringLiteral("MONEY"),QStringLiteral("TIME"),QStringLiteral("ITEM"),QStringLiteral("ITEMSALES"),QStringLiteral("NOITEM"),QStringLiteral("BOUGHT"),QStringLiteral("PBAND"),QStringLiteral("FLAG"),QStringLiteral("TFLAG"),QStringLiteral("TARGET"),QStringLiteral("MASTER"),QStringLiteral("PLAYER"),QStringLiteral("ASSI"),QStringLiteral("ASSIPLAY"),QStringLiteral("UP"),QStringLiteral("DOWN"),QStringLiteral("LOSEBASE"),QStringLiteral("PALAMLV"),QStringLiteral("EXPLV"),QStringLiteral("EJAC"),QStringLiteral("PREVCOM"),QStringLiteral("SELECTCOM"),QStringLiteral("NEXTCOM"),QStringLiteral("RESULT"),QStringLiteral("COUNT"),QStringLiteral("A"),QStringLiteral("B"),QStringLiteral("C"),QStringLiteral("CHARANUM"),QStringLiteral("LASTLOAD_NO"),QStringLiteral("LASTLOAD_VERSION")
		};
		// 先经 REF 形参别名解析（`#DIM REF X` ← 系统变量实参），再判名。
		return kNames.contains(systemVariableName(name));
}

qint64 VariableStorage::getSystemVariable(const QString &name, int index) const
{
		// 系统变量名同样大小写不敏感（ICVariable）；并先经 REF 形参别名解析
		// （`#DIM REF ターゲット` ← TARGET 之类，见 systemVariableName）。
		const QString key = systemVariableName(name);
		// Check for system variables and call appropriate getter
		if (key == "DAY") return getDay(index);
		if (key == "MONEY") return getMoney(index);
		if (key == "TIME") return getTime(index);
		if (key == "ITEM") return getItem(index);
		if (key == "ITEMSALES") return getItemsales(index);
		if (key == "NOITEM") return getNoitem(index);
		if (key == "BOUGHT") return getBought(index);
		if (key == "PBAND") return getPband(index);
		if (key == "FLAG") return getFlag(index);
		if (key == "TFLAG") return getTflag(index);
		if (key == "TARGET") return getTarget(index);
		if (key == "MASTER") return getMaster(index);
		if (key == "PLAYER") return getPlayer(index);
		if (key == "ASSI") return getAssi(index);
		if (key == "ASSIPLAY") return getAssiplay(index);
		if (key == "UP") return getUp(index);
		if (key == "DOWN") return getDown(index);
		if (key == "LOSEBASE") return getLosebase(index);
		if (key == "PALAMLV") return getPalamlv(index);
		if (key == "EXPLV") return getExplv(index);
		if (key == "EJAC") return getEjac(index);
		if (key == "PREVCOM") return getPrevcom(index);
		if (key == "SELECTCOM") return getSelectcom(index);
		if (key == "NEXTCOM") return getNextcom(index);
		if (key == "RESULT") return getResult(index);
		if (key == "COUNT") return getCount(index);
		if (key == "A") return getA(index);
		if (key == "B") return getB(index);
		if (key == "C") return getC(index);
		// CHARANUM：已登记角色数（对齐 C# VEvaluator.CHARANUM，由 ADDCHARA/DELCHARA 维护）
		if (key == "CHARANUM") return m_charaList.size();
		
		if(key=="LASTLOAD_NO" || key=="LASTLOAD_VERSION") return m_systemIntVars.value(key);
		// Not a system variable, return 0
		return 0;
}

void VariableStorage::setSystemVariable(const QString &name, int index, qint64 value)
{
		// 系统变量名同样大小写不敏感（ICVariable）；并先经 REF 形参别名解析
		const QString key = systemVariableName(name);
		if(key=="LASTLOAD_NO" || key=="LASTLOAD_VERSION") { if(index==0) m_systemIntVars[key]=value; return; }
        // Check for system variables and call appropriate setter
		if (key == "DAY") { setDay(index, value); return; }
		if (key == "MONEY") { setMoney(index, value); return; }
		if (key == "TIME") { setTime(index, value); return; }
		if (key == "ITEM") { setItem(index, value); return; }
		if (key == "ITEMSALES") { setItemsales(index, value); return; }
		if (key == "NOITEM") { setNoitem(index, value); return; }
		if (key == "BOUGHT") { setBought(index, value); return; }
		if (key == "PBAND") { setPband(index, value); return; }
		if (key == "FLAG") { setFlag(index, value); return; }
		if (key == "TFLAG") { setTflag(index, value); return; }
		if (key == "TARGET") { setTarget(index, value); return; }
		if (key == "MASTER") { setMaster(index, value); return; }
		if (key == "PLAYER") { setPlayer(index, value); return; }
		if (key == "ASSI") { setAssi(index, value); return; }
		if (key == "ASSIPLAY") { setAssiplay(index, value); return; }
		if (key == "UP") { setUp(index, value); return; }
		if (key == "DOWN") { setDown(index, value); return; }
		if (key == "LOSEBASE") { setLosebase(index, value); return; }
		if (key == "PALAMLV") { setPalamlv(index, value); return; }
		if (key == "EXPLV") { setExplv(index, value); return; }
		if (key == "EJAC") { setEjac(index, value); return; }
		if (key == "PREVCOM") { setPrevcom(index, value); return; }
		if (key == "SELECTCOM") { setSelectcom(index, value); return; }
		if (key == "NEXTCOM") { setNextcom(index, value); return; }
		if (key == "RESULT") { setResult(index, value); return; }
		if (key == "COUNT") { setCount(index, value); return; }
		if (key == "A") { setA(index, value); return; }
		if (key == "B") { setB(index, value); return; }
		if (key == "C") { setC(index, value); return; }
}

namespace {
struct SaveShape { QString name; bool string = false; QList<int> dimensions; bool user = false; };
QList<SaveShape> saveShapes(const VariableConfig& config, const VariableTable* table,
                           bool character, bool global) {
    QList<SaveShape> shapes;
    const auto add = [&](const QString& name, bool string, int rank) {
        QList<int> dims;
        if (rank == 1) dims = {qMax(0, config.getSize1D(name))};
        if (rank == 2) { auto d = config.getSize2D(name); dims = {qMax(0,d.first),qMax(0,d.second)}; }
        if (rank == 3) { auto d = config.getSize3D(name); dims = {qMax(0,d.value(0)),qMax(0,d.value(1)),qMax(0,d.value(2))}; }
        shapes.append({name,string,dims,false});
    };
    const auto words = [](const char* text) { return QString::fromLatin1(text).split(' ', Qt::SkipEmptyParts); };
    if (global) { add("GLOBAL",false,1); add("GLOBALS",true,1); }
    else if (character) {
        for (const auto& n : words("ISASSI NO")) add(n,false,0);
        for (const auto& n : words("NAME CALLNAME NICKNAME MASTERNAME")) add(n,true,0);
        for (const auto& n : words("BASE MAXBASE ABL TALENT EXP MARK PALAM SOURCE EX CFLAG JUEL RELATION EQUIP TEQUIP STAIN GOTJUEL NOWEX DOWNBASE CUP CDOWN TCVAR")) add(n,false,1);
        add("CSTR",true,1); add("CDFLAG",false,2);
    } else {
        for (const auto& n : words("DAY MONEY ITEM FLAG TFLAG UP PALAMLV EXPLV EJAC DOWN RESULT COUNT TARGET ASSI MASTER NOITEM LOSEBASE SELECTCOM ASSIPLAY PREVCOM TIME ITEMSALES PLAYER NEXTCOM PBAND BOUGHT RANDDATA")) add(n,false,1);
        for (char c='A'; c<='Z'; ++c) add(QString(QChar(c)),false,1);
        add("SAVESTR",true,1); add("TSTR",true,1);
        for (const auto& n : words("DITEMTYPE DA DB DC DD DE")) add(n,false,2);
        add("TA",false,3); add("TB",false,3);
    }
    if (table) for (const auto& d : table->declarations()) {
        if (!d.isSavedata || d.isConst || d.isReference || d.isArg
            || d.scope != VarScope::Global || d.isCharaData != character || d.isGlobal != global) continue;
        // The reference character loader has no 3D case.
        if (character && d.dimension > 2) continue;
        shapes.append({d.name,d.type == OperandType::Str,d.lengths.isEmpty() ? QList<int>{1} : d.lengths,true});
    }
    return shapes;
}
int shapeCount(const QList<int>& dims) {
    qint64 count=1;
    for (int n : dims) { if (n < 0 || n > 16*1024*1024 || (n && count > 16*1024*1024/n)) return 0; count*=n; }
    return int(count);
}
QList<int> coordinates(int index, const QList<int>& dims) {
    QList<int> result(dims.size());
    for (int axis=dims.size()-1;axis>=0;--axis) { result[axis]=dims[axis] ? index%dims[axis] : 0; if(dims[axis]) index/=dims[axis]; }
    return result;
}
}

EraBinaryData::Document VariableStorage::captureBinarySave(EraBinaryData::FileType type,
                                                           const QList<int>& selected) const {
    auto* self=const_cast<VariableStorage*>(this);
    const auto context=localContext(); self->setLocalContext({});
    EraBinaryData::Document document; document.type=type;
    const auto capture = [&](const SaveShape& s, int character) {
        EraBinaryData::Variable v; v.name=s.name; v.string=s.string; v.dimensions=s.dimensions;
        const int n=shapeCount(s.dimensions);
        for(int i=0;i<n;++i) {
            const auto p=coordinates(i,s.dimensions); const int x=p.value(0),y=p.value(1),z=p.value(2);
            if(character>=0) {
                if(s.string) v.strings.append(s.dimensions.size()<2 ? getCharaStr(s.name,character,x)
                    : m_charaStr2D.value(s.name).value(resolveCharaIndex(character)).value(x).value(y).text);
                else v.integers.append(s.dimensions.size()<2 ? getCharaInt(s.name,character,x) : getCharaInt3D(s.name,character,x,y));
            } else if(s.string) {
                v.strings.append(s.dimensions.size()<2 ? getGlobalStr1D(s.name,x) : s.dimensions.size()==2 ? getGlobalStr2D(s.name,x,y) : m_globalStr3D.value(s.name).value(x).value(y).value(z).text);
            } else v.integers.append(s.dimensions.size()<2 ? (hasSystemVariable(s.name) ? getSystemVariable(s.name,x) : getGlobalInt1D(s.name,x))
                                      : s.dimensions.size()==2 ? getGlobalInt2D(s.name,x,y) : getGlobalInt3D(s.name,x,y,z));
        }
        return v;
    };
    if(type==EraBinaryData::FileType::Normal || type==EraBinaryData::FileType::Character) {
        QList<int> indexes=selected;
        if(indexes.isEmpty()) for(int i=0;i<charaNum();++i) indexes.append(i);
        const auto shapes=saveShapes(m_variableConfig,m_saveVariableTable,true,false);
        for(int index : indexes) {
            if(index<0 || index>=charaNum()) continue;
            EraBinaryData::Character c;
            for(const auto& s:shapes) (s.user ? c.userDefined : c.builtins).append(capture(s,index));
            document.characters.append(c);
        }
    }
    if(type!=EraBinaryData::FileType::Character)
        for(const auto& s:saveShapes(m_variableConfig,m_saveVariableTable,false,type==EraBinaryData::FileType::Global)) document.variables.append(capture(s,-1));
    self->setLocalContext(context);
    return document;
}

void VariableStorage::applyBinarySave(const EraBinaryData::Document& document) {
    const bool normal=document.type==EraBinaryData::FileType::Normal;
    // GLOBAL is a separate lifetime and does not belong to a normal load.
    const auto globals=normal ? captureBinarySave(EraBinaryData::FileType::Global) : EraBinaryData::Document{};
    // Keep every GLOBAL declaration, including tokens that are deliberately not SAVEDATA.
    QSet<QString> globalKeys{QStringLiteral("GLOBAL"), QStringLiteral("GLOBALS")};
    if (m_saveVariableTable) for (const auto& d : m_saveVariableTable->declarations())
        if (d.isGlobal) globalKeys.insert(d.name.toUpper());
    const auto preserve = [&](const auto& map) {
        auto kept = map;
        for (auto it = kept.begin(); it != kept.end(); )
            if (!globalKeys.contains(it.key().toUpper())) it = kept.erase(it); else ++it;
        return kept;
    };
    const auto keptI1=preserve(m_globalInt1D); const auto keptI2=preserve(m_globalInt2D); const auto keptI3=preserve(m_globalInt3D);
    const auto keptS1=preserve(m_globalStr1D); const auto keptS2=preserve(m_globalStr2D); const auto keptS3=preserve(m_globalStr3D);
    if(normal) {
        resetForNewGame(); setLocalContext({});
        if(m_saveVariableTable) for(const auto& d:m_saveVariableTable->declarations()) {
            if(d.isConst || d.isReference || d.isCharaData || d.isGlobal) continue;
            const QString key=d.scope==VarScope::Local ? d.function.toUpper()+QChar(0x1f)+d.name.toUpper() : d.name;
            for(int i=0;i<shapeCount(d.lengths.isEmpty() ? QList<int>{1} : d.lengths);++i) {
                const auto p=coordinates(i,d.lengths); int x=p.value(0),y=p.value(1),z=p.value(2);
                if(d.type==OperandType::Str) {
                    const QString value=d.defaultStr.value(i);
                    if(d.dimension<=1) setGlobalStr1D(key,x,value);
                    else if(d.dimension==2) setGlobalStr2D(key,x,y,value);
                    else { auto& a=m_globalStr3D[key]; if(a.size()<=x)a.resize(x+1); if(a[x].size()<=y)a[x].resize(y+1); if(a[x][y].size()<=z)a[x][y].resize(z+1); a[x][y][z]=flatString(QVariant(value)); }
                } else {
                    qint64 value=d.defaultInt.value(i);
                    if(d.dimension<=1) setGlobalInt1D(key,x,value);
                    else if(d.dimension==2) setGlobalInt2D(key,x,y,value);
                    else setGlobalInt3D(key,x,y,z,value);
                }
            }
        }
        applyBinarySave(globals);
        const auto restore=[](auto& map,const auto& kept) { for(auto it=kept.begin();it!=kept.end();++it) map.insert(it.key(),it.value()); };
        restore(m_globalInt1D,keptI1); restore(m_globalInt2D,keptI2); restore(m_globalInt3D,keptI3);
        restore(m_globalStr1D,keptS1); restore(m_globalStr2D,keptS2); restore(m_globalStr3D,keptS3);
    }
    const auto apply = [&](const EraBinaryData::Variable& v,const SaveShape& s,int character) {
        if(v.string!=s.string || v.dimensions.size()!=s.dimensions.size()) return;
        for(int i=0;i<shapeCount(s.dimensions);++i) {
            const auto p=coordinates(i,s.dimensions); int source=0; bool overlap=true;
            for(int axis=0;axis<p.size();++axis) { if(p[axis]>=v.dimensions[axis]) overlap=false; source=source*v.dimensions[axis]+p[axis]; }
            const qint64 number=overlap ? v.integers.value(source) : 0;
            const QString text=overlap ? v.strings.value(source) : QString();
            const int x=p.value(0),y=p.value(1),z=p.value(2);
            if(character>=0) {
                if(s.string) {
                    if(p.size()<2) setCharaStr(s.name,character,x,text);
                    else { auto& a=m_charaStr2D[s.name]; int slot=resolveCharaIndex(character); if(a.size()<=slot)a.resize(slot+1); if(a[slot].size()<=x)a[slot].resize(x+1); if(a[slot][x].size()<=y)a[slot][x].resize(y+1); a[slot][x][y]=flatString(QVariant(text)); }
                } else if(p.size()<2) setCharaInt(s.name,character,x,number); else setCharaInt3D(s.name,character,x,y,number);
            } else if(s.string) {
                if(p.size()<2) setGlobalStr1D(s.name,x,text); else if(p.size()==2) setGlobalStr2D(s.name,x,y,text);
                else { auto& a=m_globalStr3D[s.name]; if(a.size()<=x)a.resize(x+1); if(a[x].size()<=y)a[x].resize(y+1); if(a[x][y].size()<=z)a[x][y].resize(z+1); a[x][y][z]=flatString(QVariant(text)); }
            } else if(p.size()<2) { if(hasSystemVariable(s.name)) setSystemVariable(s.name,x,number); else setGlobalInt1D(s.name,x,number); }
            else if(p.size()==2) setGlobalInt2D(s.name,x,y,number); else setGlobalInt3D(s.name,x,y,z,number);
        }
    };
    const auto applyList = [&](const QList<EraBinaryData::Variable>& list,const QList<SaveShape>& shapes,int character) {
        for(const auto& v:list) for(const auto& s:shapes) if(v.name==s.name) { apply(v,s,character); break; }
    };
    if(document.type==EraBinaryData::FileType::Normal || document.type==EraBinaryData::FileType::Character) {
        const auto shapes=saveShapes(m_variableConfig,m_saveVariableTable,true,false);
        for(const auto& c:document.characters) {
            // Runtime slots must be distinct even when NO is repeated or the original list was reordered.
            int slot=0; for(int n:m_charaList) slot=qMax(slot,n+1);
            const int index=m_charaList.size(); m_charaList.append(slot);
            for(const auto& s:shapes) {
                EraBinaryData::Variable zero; zero.name=s.name; zero.string=s.string; zero.dimensions=s.dimensions;
                if(s.string) zero.strings.resize(shapeCount(s.dimensions)); else zero.integers.resize(shapeCount(s.dimensions));
                apply(zero,s,index);
            }
            applyList(c.builtins,shapes,index); applyList(c.userDefined,shapes,index);
        }
    }
    auto shapes=saveShapes(m_variableConfig,m_saveVariableTable,false,document.type==EraBinaryData::FileType::Global);
    if(document.type==EraBinaryData::FileType::Var) shapes.append(saveShapes(m_variableConfig,m_saveVariableTable,false,true));
    applyList(document.variables,shapes,-1);
}

// Compatibility adapters now read/write the actual slot; no provenance sidecar.
QVariant VariableStorage::stringValueRaw(const QString& key, int index, const QString&) const {
    const auto slot = getStringSlot(key, {index});
    return slot.markup.parts.isEmpty() ? QVariant(slot.text) : toVariant(slot);
}
void VariableStorage::setStringProvenance(const QString& name, int index, const QVariant& value) {
    setStringSlot(name, {index}, flatString(value));
}

FlatStringValue VariableStorage::getStringSlot(const QString& name, const QList<int>& indices) const {
    const QString key = storageName(name);
    const int x = indices.value(0), y = indices.value(1), z = indices.value(2);
    if (x < 0 || y < 0 || z < 0) return {};
    if (key == "LOCALS") return m_localStrVars.value(x);
    if (key == "ARGS") return m_argStrVars.value(x);
    if (key == "SAVEDATA_TEXT") return m_systemStrVars.value(key);
    if (isCharaDataString(name)) {
        int character; QList<int> elements; reduceCharaArgs(name, indices, character, elements);
        const int slot = resolveCharaIndex(character);
        if (charaDataDimension(name) >= 2)
            return m_charaStr2D.value(key).value(slot).value(elements.value(0)).value(elements.value(1));
        return m_charaStrVars.value(key).value(slot).value(elements.value(0));
    }
    if (indices.size() >= 3) return m_globalStr3D.value(key).value(x).value(y).value(z);
    if (indices.size() >= 2) return m_globalStr2D.value(key).value(x).value(y);
    return m_globalStr1D.value(key).value(x);
}
bool VariableStorage::setStringSlot(const QString& name, const QList<int>& indices, const FlatStringValue& value) {
    const QString key = storageName(name);
    const int x = indices.value(0), y = indices.value(1), z = indices.value(2);
    if (x < 0 || y < 0 || z < 0) return false;
    const auto put1 = [&](auto& list, int i) { if (list.size() <= i) list.resize(i+1); list[i] = value; };
    const auto put2 = [&](auto& list, int i, int j) { if (list.size() <= i) list.resize(i+1); put1(list[i], j); };
    const auto put3 = [&](auto& list, int i, int j, int k) { if (list.size() <= i) list.resize(i+1); put2(list[i], j, k); };
    if (key == "LOCALS") put1(m_localStrVars, x);
    else if (key == "ARGS") put1(m_argStrVars, x);
    else if (key == "SAVEDATA_TEXT") m_systemStrVars[key] = value;
    else if (isCharaDataString(name)) {
        int character; QList<int> elements; reduceCharaArgs(name, indices, character, elements);
        const int slot = resolveCharaIndex(character);
        if (slot < 0 || elements.value(0) < 0 || elements.value(1) < 0) return false;
        if (charaDataDimension(name) >= 2) put3(m_charaStr2D[key], slot, elements.value(0), elements.value(1));
        else put2(m_charaStrVars[key], slot, elements.value(0));
    }
    else if (indices.size() >= 3) put3(m_globalStr3D[key], x, y, z);
    else if (indices.size() >= 2) put2(m_globalStr2D[key], x, y);
    else put1(m_globalStr1D[key], x);
    return true;
}
