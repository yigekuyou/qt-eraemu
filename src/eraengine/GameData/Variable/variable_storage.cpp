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
				m_localStrVars.fill(QString(), localSize);
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
		charaId = resolveCharaIndex(charaId);
		if (charaId < 0) return;   // VOID 角色：无模板数据
		auto &charaList = m_charaStrVars[storageName(name)];
		if (charaList.size() <= charaId) charaList.resize(charaId + 1);
		auto &vec = charaList[charaId];
		if (vec.size() <= index) vec.resize(index + 1);
		vec[index] = value;
}

QString VariableStorage::getCharaStr(const QString &name, int charaId, int index) const
{
		if (charaId < 0 || index < 0) return QString();
		charaId = resolveCharaIndex(charaId);
		if (charaId < 0) return QString();   // VOID 角色：无模板数据
		auto it = m_charaStrVars.constFind(storageName(name));
		if (it != m_charaStrVars.constEnd() && charaId < it.value().size()) {
				const auto &vec = it.value().at(charaId);
				if (index < vec.size()) return vec.at(index);
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

QString VariableStorage::dumpCharaList() const
{
		QStringList out;
		for (int i = 0; i < m_charaList.size(); ++i)
			out << QString::number(m_charaList.at(i)) << (m_charaSp.contains(i) ? QStringLiteral("1") : QStringLiteral("0"));
		return out.join(QLatin1Char(' '));
}

void VariableStorage::appendCharaList(const QString& text)
{
		const QStringList f = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		for (int i = 0; i + 1 < f.size(); i += 2) {
			bool okNo = false, okSp = false;
			const int no = f.at(i).toInt(&okNo);
			const int sp = f.at(i + 1).toInt(&okSp);
			if (!okNo || no < 0) continue;
			m_charaList.append(no);
			if (okSp && sp != 0) m_charaSp.insert(m_charaList.size() - 1);
		}
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
		for (QString& s : m_localStrVars) s.clear();
		for (QString& s : m_argStrVars) s.clear();
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
		return index < vec.size() ? vec.at(index) : QString();
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
		m_localStrVars[index] = value;
}

QString VariableStorage::getLocalStr(int index) const
{
		if (index >= 0 && index < m_localStrVars.size()) {
				return m_localStrVars.at(index);
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
		m_argStrVars[index] = value;
}

QString VariableStorage::getArgStr(int index) const
{
		return (index >= 0 && index < m_argStrVars.size()) ? m_argStrVars.at(index) : QString();
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
		if (vec.size() <= x) vec.resize(x + 1, QString());
		vec[x] = value;
}

QString VariableStorage::getGlobalStr1D(const QString &name, int x) const
{
        return getGlobalStr1DRaw(storageName(name), x);
}

QString VariableStorage::getGlobalStr1DRaw(const QString& storageKey, int x) const
{
        if (x < 0) return QString();
        auto it = m_globalStr1D.constFind(storageKey);
        if (it != m_globalStr1D.constEnd() && x < it.value().size()) return it.value().at(x);
        return QString();
}

void VariableStorage::setGlobalStr2D(const QString &name, int x, int y, const QString &value)
{
		if (x < 0 || y < 0) return;
		auto &table = m_globalStr2D[storageName(name)];
		if (table.size() <= x) table.resize(x + 1);
		if (table[x].size() <= y) table[x].resize(y + 1, QString());
		table[x][y] = value;
}

QString VariableStorage::getGlobalStr2D(const QString &name, int x, int y) const
{
		if (x < 0 || y < 0) return QString();
		auto it = m_globalStr2D.constFind(storageName(name));
		if (it == m_globalStr2D.constEnd()) return QString();
		if (x >= it.value().size() || y >= it.value().at(x).size()) return QString();
		return it.value().at(x).at(y);
}

void VariableStorage::setSystemStr(const QString &name, int index, const QString &value)
{
		if (index != 0) return;   // 系统字符串变量目前只有单值（RESULTS / SAVEDATA_TEXT）
		m_systemStrVars[eraUpperKey(name)] = value;
}

QString VariableStorage::getSystemStr(const QString &name, int index) const
{
		if (index != 0) return QString();
		return m_systemStrVars.value(eraUpperKey(name));
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
QStringLiteral("DAY"),QStringLiteral("MONEY"),QStringLiteral("TIME"),QStringLiteral("ITEM"),QStringLiteral("ITEMSALES"),QStringLiteral("NOITEM"),QStringLiteral("BOUGHT"),QStringLiteral("PBAND"),QStringLiteral("FLAG"),QStringLiteral("TFLAG"),QStringLiteral("TARGET"),QStringLiteral("MASTER"),QStringLiteral("PLAYER"),QStringLiteral("ASSI"),QStringLiteral("ASSIPLAY"),QStringLiteral("UP"),QStringLiteral("DOWN"),QStringLiteral("LOSEBASE"),QStringLiteral("PALAMLV"),QStringLiteral("EXPLV"),QStringLiteral("EJAC"),QStringLiteral("PREVCOM"),QStringLiteral("SELECTCOM"),QStringLiteral("NEXTCOM"),QStringLiteral("RESULT"),QStringLiteral("COUNT"),QStringLiteral("A"),QStringLiteral("B"),QStringLiteral("C"),QStringLiteral("CHARANUM")
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
		
		// Not a system variable, return 0
		return 0;
}

void VariableStorage::setSystemVariable(const QString &name, int index, qint64 value)
{
		// 系统变量名同样大小写不敏感（ICVariable）；并先经 REF 形参别名解析
		const QString key = systemVariableName(name);
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

// ================= Expression Evaluation =================
QVariant VariableStorage::evaluateExpression(const QString &expression)
{
    ExpressionEvaluator evaluator;
    return evaluator.evaluate(expression, this);
}

// ---------------------------------------------------------------------------
// 存档序列化（SAVEDATA/LOADDATA 支持；对齐 C# VariableEvaluator.SaveTo/LoadFrom）
//   文件名 save{index:00}.sav（C# VariableEvaluator.cs:1746）；
//   格式为移植版行式文本（eraemu-save-v1）—— C# 的 EraDataWriter 格式不
//   兼容，C# 存档不可载入（已知限制）；移植版自身存档可完整往返。
//   只序列化**运行期游戏状态**：角色清单、全局变量、角色变量、系统变量。
//   局部变量（LOCAL/LOCALS/ARGS/私有变量）不入档（对齐 C#：存档只存全局）。
// ---------------------------------------------------------------------------
QString VariableStorage::dumpSaveData() const
{
    QStringList out;
    out << QStringLiteral("eraemu-save-v1");

    // 角色清单（运行时下标 -> 模板号；对齐 C# CharacterList）
    QStringList csvNos;
    for (int v : m_charaList) csvNos << QString::number(v);
    out << QStringLiteral("CHARALIST\t") + csvNos.join(QLatin1Char(','));

    // 全局整数 1D/2D
    for (auto it = m_globalInt1D.constBegin(); it != m_globalInt1D.constEnd(); ++it) {
        const auto &cells = it.value();
        for (int i = 0; i < cells.size(); ++i) {
            if (cells.at(i) != 0)
                out << QStringLiteral("GI\t%1\t%2\t%3").arg(it.key(), QString::number(i)).arg(cells.at(i));
        }
    }
    for (auto it = m_globalInt2D.constBegin(); it != m_globalInt2D.constEnd(); ++it) {
        const auto &rows = it.value();
        for (int x = 0; x < rows.size(); ++x) {
            const auto &cols = rows.at(x);
            for (int y = 0; y < cols.size(); ++y) {
                if (cols.at(y) != 0)
                    out << QStringLiteral("GI2\t%1\t%2\t%3\t%4").arg(it.key(), QString::number(x), QString::number(y)).arg(cols.at(y));
            }
        }
    }
    // 全局字符串 1D/2D
    for (auto it = m_globalStr1D.constBegin(); it != m_globalStr1D.constEnd(); ++it) {
        const auto &cells = it.value();
        for (int i = 0; i < cells.size(); ++i) {
            if (!cells.at(i).isEmpty())
                out << QStringLiteral("GS\t%1\t%2\t%3").arg(it.key(), QString::number(i), cells.at(i));
        }
    }
    for (auto it = m_globalStr2D.constBegin(); it != m_globalStr2D.constEnd(); ++it) {
        const auto &rows = it.value();
        for (int x = 0; x < rows.size(); ++x) {
            const auto &cols = rows.at(x);
            for (int y = 0; y < cols.size(); ++y) {
                if (!cols.at(y).isEmpty())
                    out << QStringLiteral("GS2\t%1\t%2\t%3\t%4").arg(it.key(), QString::number(x), QString::number(y), cols.at(y));
            }
        }
    }
    // 角色变量（int/str：m_charaIntVars / m_charaStrVars 的 charaId -> 槽位）
    for (auto it = m_charaIntVars.constBegin(); it != m_charaIntVars.constEnd(); ++it) {
        const auto &rows = it.value();
        for (int c = 0; c < rows.size(); ++c) {
            const auto &cells = rows.at(c);
            for (int i = 0; i < cells.size(); ++i) {
                if (cells.at(i) != 0)
                    out << QStringLiteral("CI\t%1\t%2\t%3\t%4").arg(it.key(), QString::number(c), QString::number(i)).arg(cells.at(i));
            }
        }
    }
    for (auto it = m_charaStrVars.constBegin(); it != m_charaStrVars.constEnd(); ++it) {
        const auto &rows = it.value();
        for (int c = 0; c < rows.size(); ++c) {
            const auto &cells = rows.at(c);
            for (int i = 0; i < cells.size(); ++i) {
                if (!cells.at(i).isEmpty())
                    out << QStringLiteral("CS\t%1\t%2\t%3\t%4").arg(it.key(), QString::number(c), QString::number(i), cells.at(i));
            }
        }
    }
    // 专用系统变量成员（DAY/MONEY/TIME/ITEM/FLAG/TFLAG/…：m_day 等专用 QList，
    //   不在 m_systemIntVars 哈希里 —— 此前漏档导致读档后 DAY/MONEY 不还原）
    //   成员名 -> 系统变量名（restore 侧经 setSystemVariable 的既有分发落位）
    // 成员名 -> 系统变量名（restore 侧经 setSystemVariable 的既有分发落位）
    struct DedicatedVar { const QList<qint64>* list; const char* name; };
    static const DedicatedVar kDedicated[] = {
        {&m_day, "DAY"},   {&m_money, "MONEY"},   {&m_time, "TIME"},
        {&m_item, "ITEM"}, {&m_itemsales, "ITEMSALES"}, {&m_noitem, "NOITEM"},
        {&m_bought, "BOUGHT"}, {&m_pband, "PBAND"}, {&m_flag, "FLAG"},
        {&m_tflag, "TFLAG"}, {&m_target, "TARGET"}, {&m_master, "MASTER"},
        {&m_player, "PLAYER"}, {&m_assi, "ASSI"}, {&m_assiplay, "ASSIPLAY"},
        {&m_up, "UP"}, {&m_down, "DOWN"}, {&m_losebase, "LOSEBASE"},
        {&m_palamlv, "PALAMLV"}, {&m_explv, "EXPLV"}, {&m_ejac, "EJAC"},
        {&m_prevcom, "PREVCOM"}, {&m_selectcom, "SELECTCOM"}, {&m_nextcom, "NEXTCOM"},
        {&m_result, "RESULT"}, {&m_count, "COUNT"},
        {&m_a, "A"}, {&m_b, "B"}, {&m_c, "C"},
    };
    for (const DedicatedVar& dv : kDedicated) {
        const QList<qint64>& cells = *dv.list;
        for (int i = 0; i < cells.size(); ++i) {
            if (cells.at(i) != 0)
                out << QStringLiteral("SI\t%1\t%2\t%3").arg(QString::fromLatin1(dv.name), QString::number(i)).arg(cells.at(i));
        }
    }
    // 其他系统变量（int/str 哈希）
    for (auto it = m_systemIntVars.constBegin(); it != m_systemIntVars.constEnd(); ++it) {
        if (it.value() != 0)
            out << QStringLiteral("SI\t%1\t%2").arg(it.key()).arg(it.value());
    }
    for (auto it = m_systemStrVars.constBegin(); it != m_systemStrVars.constEnd(); ++it) {
        if (!it.value().isEmpty())
            out << QStringLiteral("SS\t%1\t%2").arg(it.key(), it.value());
    }
    return out.join(QLatin1Char('\n'));
}

void VariableStorage::restoreSaveData(const QString& text)
{
    if (text.isEmpty() || !text.startsWith(QStringLiteral("eraemu-save-v1"))) {
        qWarning() << "[save] 存档格式不识别（eraemu-save-v1）";
        return;
    }
    const QStringList lines = text.split(QLatin1Char('\n'));
    // 先清运行期状态（对齐 C# LoadFromStream：SetDefaultValue 后再载入）
    for (const QString& line : lines) {
        const QList<QString> f = line.split(QLatin1Char('\t'));
        if (f.isEmpty()) continue;
        const QString& tag = f.first();
        if (tag == QLatin1String("CHARALIST")) {
            m_charaList.clear();
            if (f.size() >= 2 && !f.at(1).isEmpty()) {
                const QStringList nos = f.at(1).split(QLatin1Char(','));
                for (const QString& n : nos) m_charaList.append(n.toInt());
            }
        } else if (tag == QLatin1String("GI") && f.size() >= 4) {
            setGlobalInt1D(f.at(1), f.at(2).toInt(), f.at(3).toLongLong());
        } else if (tag == QLatin1String("GI2") && f.size() >= 5) {
            setGlobalInt2D(f.at(1), f.at(2).toInt(), f.at(3).toInt(), f.at(4).toLongLong());
        } else if (tag == QLatin1String("GS") && f.size() >= 4) {
            setGlobalStr1D(f.at(1), f.at(2).toInt(), f.at(3));
        } else if (tag == QLatin1String("GS2") && f.size() >= 5) {
            setGlobalStr2D(f.at(1), f.at(2).toInt(), f.at(3).toInt(), f.at(4));
        } else if (tag == QLatin1String("CI") && f.size() >= 5) {
            setCharaInt(f.at(1), f.at(2).toInt(), f.at(3).toInt(), f.at(4).toLongLong());
        } else if (tag == QLatin1String("CS") && f.size() >= 5) {
            setCharaStr(f.at(1), f.at(2).toInt(), f.at(3).toInt(), f.at(4));
        } else if (tag == QLatin1String("SI")) {
            // 格式 SI\t<名>\t<下标>\t<值>；无下标形态（3 字段）按标量
            if (f.size() >= 4)
                setSystemVariable(f.at(1), f.at(2).toInt(), f.at(3).toLongLong());
            else if (f.size() >= 3)
                setSystemVariable(f.at(1), 0, f.at(2).toLongLong());
        } else if (tag == QLatin1String("SS")) {
            // 格式 SS\t<名>\t<下标>\t<值>；无下标形态（3 字段）按标量
            if (f.size() >= 4)
                setSystemStr(f.at(1).toUpper(), f.at(2).toInt(), f.at(3));
            else if (f.size() >= 3)
                setSystemStr(f.at(1).toUpper(), 0, f.at(2));
        }
    }
    qCDebug(eraTrace) << "[save] 存档载入：行" << lines.size();
}
