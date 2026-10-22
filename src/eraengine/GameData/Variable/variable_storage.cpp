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
#include <cstring>
#include <algorithm>
#include <limits>
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

// ---------------------------------------------------------------------------
// 多维数组（dimension >= 2）
//
// 存储形态与单维**不同**：单维是 m_globalInt1D / m_globalStr1D 的轻量 QList，
// 多维是这里的一张扁平表（m_globalIntN / m_globalStrN）+ 装载期算好的 strides。
// 形状（lengths/strides/elementCount）来自 VariableDecl；本类只持有一份装载期
// 拷入的视图（QList 是 COW），运行期只读。
// ---------------------------------------------------------------------------
std::optional<qint64> VariableStorage::flattenMultiDim(const QString& storageKey,
                                                       const QList<int>& indices,
                                                       bool warnOnBound,
                                                       const char* operation) const
{
    const auto it = m_multiDimShapes.constFind(storageKey);
    if (it == m_multiDimShapes.constEnd()) return std::nullopt;
    const MultiDimShape& shape = it.value();
    const auto reject = [&](const QString& why) -> std::optional<qint64> {
        if (warnOnBound)
            qWarning().noquote() << "[var] 多维变量" << storageKey << operation
                                 << "越界（忽略）:" << why << "indices=" << indices;
        return std::nullopt;
    };
    if (shape.lengths.isEmpty() || shape.elementCount <= 0)
        return reject(QStringLiteral("形状未定稿"));
    if (indices.isEmpty()) return qint64(0);   // 裸名 = 元素 0（与 1D 口径一致）

    qint64 flat = 0;
    if (indices.size() == 1) {
        // 扁平写法：X[flat] —— 直接按元素序号寻址（与 X:i:j:k 共用同一个容器）。
        flat = indices.at(0);
    } else {
        const int rank = int(shape.lengths.size());
        for (int i = 0; i < indices.size(); ++i) {
            if (i >= rank || i >= shape.strides.size())
                return reject(QStringLiteral("下标个数超过维数"));
            const int index = indices.at(i);
            if (index < 0 || index >= shape.lengths.at(i))
                return reject(QStringLiteral("第 %1 维下标 %2 不在 [0,%3)")
                                  .arg(i).arg(index).arg(shape.lengths.at(i)));
            flat += qint64(index) * shape.strides.at(i);
        }
        // 缺省的后导维按 0（例如 3D 变量只给 2 个下标）。
    }
    if (flat < 0 || flat >= shape.elementCount)
        return reject(QStringLiteral("扁平偏移 %1 不在 [0,%2)").arg(flat).arg(shape.elementCount));
    return flat;
}

qint64 VariableStorage::readMultiDimInt(const QString& storageKey, const QList<int>& indices) const
{
    const auto shapeIt = m_multiDimShapes.constFind(storageKey);
    if (shapeIt == m_multiDimShapes.constEnd()) return 0;
    if (!shapeIt.value().allocated) return 0;
    const auto flat = flattenMultiDim(storageKey, indices, /*warnOnBound=*/false, "读");
    if (!flat) return 0;
    const auto it = m_globalIntN.constFind(storageKey);
    if (it == m_globalIntN.constEnd() || *flat >= it.value().size()) return 0;
    return it.value().at(*flat);
}

void VariableStorage::writeMultiDimInt(const QString& storageKey, const QList<int>& indices,
                                       qint64 value)
{
    const auto shapeIt = m_multiDimShapes.constFind(storageKey);
    if (shapeIt == m_multiDimShapes.constEnd()) return;
    if (!shapeIt.value().allocated) {
        qWarning().noquote() << "[var] 多维变量" << storageKey
                             << "未分配（元素数" << shapeIt.value().elementCount
                             << "超过上限" << kMaxMultiDimElements << "或维数未定稿），写忽略";
        return;
    }
    const auto flat = flattenMultiDim(storageKey, indices, /*warnOnBound=*/true, "写");
    if (!flat) return;
    auto it = m_globalIntN.find(storageKey);
    if (it == m_globalIntN.end() || *flat >= it.value().size()) {
        qWarning().noquote() << "[var] 多维变量" << storageKey << "存储缺失，写忽略";
        return;
    }
    it.value()[*flat] = value;
}

FlatStringValue VariableStorage::readMultiDimStr(const QString& storageKey,
                                                 const QList<int>& indices) const
{
    const auto shapeIt = m_multiDimShapes.constFind(storageKey);
    if (shapeIt == m_multiDimShapes.constEnd() || !shapeIt.value().allocated) return {};
    const auto flat = flattenMultiDim(storageKey, indices, /*warnOnBound=*/false, "读");
    if (!flat) return {};
    const auto it = m_globalStrN.constFind(storageKey);
    if (it == m_globalStrN.constEnd() || *flat >= it.value().size()) return {};
    return it.value().at(*flat);
}

void VariableStorage::writeMultiDimStr(const QString& storageKey, const QList<int>& indices,
                                       const FlatStringValue& value)
{
    const auto shapeIt = m_multiDimShapes.constFind(storageKey);
    if (shapeIt == m_multiDimShapes.constEnd()) return;
    if (!shapeIt.value().allocated) {
        qWarning().noquote() << "[var] 多维变量" << storageKey
                             << "未分配（元素数" << shapeIt.value().elementCount
                             << "超过上限" << kMaxMultiDimElements << "或维数未定稿），写忽略";
        return;
    }
    const auto flat = flattenMultiDim(storageKey, indices, /*warnOnBound=*/true, "写");
    if (!flat) return;
    auto it = m_globalStrN.find(storageKey);
    if (it == m_globalStrN.end() || *flat >= it.value().size()) {
        qWarning().noquote() << "[var] 多维变量" << storageKey << "存储缺失，写忽略";
        return;
    }
    it.value()[*flat] = value;
}

qint64 VariableStorage::multiDimAllocatedElements() const
{
    qint64 n = 0;
    for (auto it = m_globalIntN.constBegin(); it != m_globalIntN.constEnd(); ++it) n += it.value().size();
    for (auto it = m_globalStrN.constBegin(); it != m_globalStrN.constEnd(); ++it) n += it.value().size();
    return n;
}

qint64 VariableStorage::multiDimAllocatedBytes() const
{
    qint64 bytes = 0;
    for (auto it = m_globalIntN.constBegin(); it != m_globalIntN.constEnd(); ++it)
        bytes += qint64(it.value().size()) * qint64(sizeof(qint64));
    for (auto it = m_globalStrN.constBegin(); it != m_globalStrN.constEnd(); ++it)
        bytes += qint64(it.value().size()) * qint64(sizeof(FlatStringValue));
    return bytes;
}

QList<int> VariableStorage::multiDimLengths(const QString& storageKey) const
{
    const auto it = m_multiDimShapes.constFind(storageKey);
    return it == m_multiDimShapes.constEnd() ? QList<int>() : it.value().lengths;
}

void VariableStorage::installMultiDimArray(const QString& storageKey, const QList<int>& lengths,
                                           const QList<int>& strides, bool stringArray,
                                           bool allocate)
{
    if (storageKey.isEmpty() || lengths.size() < 2) return;
    qint64 count = 1;
    for (int n : lengths) {
        if (n <= 0) return;                  // 维数未定稿：不注册（运行期仍走旧路径）
        if (count > std::numeric_limits<qint64>::max() / n) { count = -1; break; }
        count *= n;
    }
    if (count <= 0) return;
    const auto existing = m_multiDimShapes.constFind(storageKey);
    if (existing != m_multiDimShapes.constEnd()) {
        // 同键同形状：幂等（装载 pass 与按需分配都可能调用）。
        if (existing.value().lengths == lengths) return;
        qWarning().noquote() << "[var] 多维变量" << storageKey << "形状冲突，保留首次声明";
        return;
    }
    MultiDimShape shape;
    shape.lengths = lengths;
    shape.elementCount = count;
    if (strides.size() == lengths.size()) {
        shape.strides = strides;
    } else {
        // 兜底：调用方没给 strides（测试/旧构造）时按「最后一维连续」补一次。
        shape.strides.resize(lengths.size());
        int acc = 1;
        for (int i = lengths.size() - 1; i >= 0; --i) {
            shape.strides[i] = acc;
            acc *= lengths.at(i);
        }
    }
    if (count > kMaxMultiDimElements) {
        // 不分配，但保留声明形状：越界判定/VARSIZE 仍与声明一致。
        qWarning().noquote() << "[var] 多维变量" << storageKey << "元素数" << count
                             << "超过上限" << kMaxMultiDimElements << "，拒绝分配";
        m_multiDimShapes.insert(storageKey, shape);
        return;
    }
    shape.allocated = allocate;
    m_multiDimShapes.insert(storageKey, shape);
    if (!allocate) return;   // 只登记形状（CHARADATA：元素在角色存储里）
    if (stringArray)
        m_globalStrN.insert(storageKey, QList<FlatStringValue>(qsizetype(count), FlatStringValue{}));
    else
        m_globalIntN.insert(storageKey, QList<qint64>(qsizetype(count), 0));
}

void VariableStorage::clearForReload()
{
    VariableStorage defaults;
    std::swap(m_globalStr3D, defaults.m_globalStr3D);
    std::swap(m_globalIntN, defaults.m_globalIntN);
    std::swap(m_globalStrN, defaults.m_globalStrN);
    std::swap(m_multiDimShapes, defaults.m_multiDimShapes);
    std::swap(m_charaInt2D, defaults.m_charaInt2D);
    std::swap(m_charaStr2D, defaults.m_charaStr2D);
    std::swap(m_systemIntVars, defaults.m_systemIntVars);
    std::swap(m_systemStrVars, defaults.m_systemStrVars);
    std::swap(m_globalInt1D, defaults.m_globalInt1D);
    std::swap(m_globalInt2D, defaults.m_globalInt2D);
    std::swap(m_globalInt3D, defaults.m_globalInt3D);
    std::swap(m_globalStr1D, defaults.m_globalStr1D);
    std::swap(m_globalStr2D, defaults.m_globalStr2D);
    std::swap(m_charaIntVars, defaults.m_charaIntVars);
    std::swap(m_charaIntVars3D, defaults.m_charaIntVars3D);
    std::swap(m_charaStrVars, defaults.m_charaStrVars);
    std::swap(m_parameters, defaults.m_parameters);
    std::swap(m_privateNames, defaults.m_privateNames);
    std::swap(m_references, defaults.m_references);
    std::swap(m_localIntVars, defaults.m_localIntVars);
    std::swap(m_argIntVars, defaults.m_argIntVars);
    std::swap(m_localStrVars, defaults.m_localStrVars);
    std::swap(m_argStrVars, defaults.m_argStrVars);
    std::swap(m_localAliases, defaults.m_localAliases);
    std::swap(m_day, defaults.m_day);
    std::swap(m_money, defaults.m_money);
    std::swap(m_time, defaults.m_time);
    std::swap(m_item, defaults.m_item);
    std::swap(m_itemsales, defaults.m_itemsales);
    std::swap(m_noitem, defaults.m_noitem);
    std::swap(m_bought, defaults.m_bought);
    std::swap(m_pband, defaults.m_pband);
    std::swap(m_flag, defaults.m_flag);
    std::swap(m_tflag, defaults.m_tflag);
    std::swap(m_target, defaults.m_target);
    std::swap(m_master, defaults.m_master);
    std::swap(m_player, defaults.m_player);
    std::swap(m_assi, defaults.m_assi);
    std::swap(m_assiplay, defaults.m_assiplay);
    std::swap(m_up, defaults.m_up);
    std::swap(m_down, defaults.m_down);
    std::swap(m_losebase, defaults.m_losebase);
    std::swap(m_palamlv, defaults.m_palamlv);
    std::swap(m_explv, defaults.m_explv);
    std::swap(m_ejac, defaults.m_ejac);
    std::swap(m_prevcom, defaults.m_prevcom);
    std::swap(m_selectcom, defaults.m_selectcom);
    std::swap(m_nextcom, defaults.m_nextcom);
    std::swap(m_result, defaults.m_result);
    std::swap(m_count, defaults.m_count);
    std::swap(m_a, defaults.m_a);
    std::swap(m_b, defaults.m_b);
    std::swap(m_c, defaults.m_c);
    std::swap(m_charaDataVars, defaults.m_charaDataVars);
    std::swap(m_charaList, defaults.m_charaList);
    std::swap(m_charaSp, defaults.m_charaSp);
    std::swap(m_existCsv, defaults.m_existCsv);
    std::swap(m_csvIntVars, defaults.m_csvIntVars);
    std::swap(m_csvIntVars3D, defaults.m_csvIntVars3D);
    std::swap(m_csvStrVars, defaults.m_csvStrVars);
    std::swap(m_variableTypes, defaults.m_variableTypes);
    std::swap(m_variableIdentifiers, defaults.m_variableIdentifiers);
    std::swap(m_variableConfig, defaults.m_variableConfig);
    // 名表随 m_privateNames 一起作废（重载后由装载收尾重新安装）。
    m_privateTableInstalled = false;
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
		resize1D(QStringLiteral("ITEMPRICE"), m_itemprice);
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
        // 多维：尺寸口径 = **声明首维**（C# VARSIZE(var) 的 GetLength() 语义；
        // VARSIZE 语句另按声明逐维写 RESULT）。不再读 1D 槽的「首维占位」。
        if (const auto shape = m_multiDimShapes.constFind(key); shape != m_multiDimShapes.constEnd())
            return shape.value().lengths.value(0);

		// 局部/实参槽不在 m_global* 容器里，单独报尺寸（VARSIZE("LOCAL")）
		if (key == QLatin1String("ITEMPRICE")) return m_itemprice.size();   // 常量容器（不在 m_global* 里）
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
    // 多维：形状的唯一运行时视图（装载期从声明拷入）。
    if (const auto shape = m_multiDimShapes.constFind(key); shape != m_multiDimShapes.constEnd())
        return shape.value().lengths;
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
		// 多维变量的容量由声明决定，不允许「第一次写时偷偷扩容」。
		if (isMultiDimArray(storageName(name))) return;
		if (stringArray) {
			auto& values = m_globalStr1D[storageName(name)];
			if (values.size() < size) values.resize(size);
		} else {
			auto& values = m_globalInt1D[storageName(name)];
			if (values.size() < size) values.resize(size);
		}
}

// 私有初值直写（调用计划）：键已解析（FUNC\x1fNAME 复合键），
// 不再走 storageName 的作用域映射（复合键不在 m_references/m_privateNames 里，
// 解析恒为恒等 —— 与「作用域内裸名」两种写法结果一致）。
void VariableStorage::applyPrivateDefault(const QString& storageKey, bool isString, int ensureSize,
                                          const QList<qint64>& defaultInt, const QStringList& defaultStr)
{
		// 多维：容量已在装载期按声明分配；初值按**扁平序号**写（行主序，最后一维
		// 连续），与 #DIM X, 2,3 = a,b,c,d 的 C# 口径一致。
		if (isMultiDimArray(storageKey)) {
			if (isString) {
				for (int i = 0; i < defaultStr.size(); ++i)
					writeMultiDimStr(storageKey, {i}, FlatStringValue(defaultStr.at(i)));
			} else {
				for (int i = 0; i < defaultInt.size(); ++i)
					writeMultiDimInt(storageKey, {i}, defaultInt.at(i));
			}
			return;
		}
		if (isString) {
			auto& values = m_globalStr1D[storageKey];
			if (ensureSize > 0 && values.size() < ensureSize) values.resize(ensureSize);
			for (int i = 0; i < defaultStr.size(); ++i) {
				if (values.size() <= i) values.resize(i + 1);
				values[i] = FlatStringValue(defaultStr.at(i));
			}
		} else {
			auto& values = m_globalInt1D[storageKey];
			if (ensureSize > 0 && values.size() < ensureSize) values.resize(ensureSize);
			for (int i = 0; i < defaultInt.size(); ++i) {
				if (values.size() <= i) values.resize(i + 1);
				values[i] = defaultInt.at(i);
			}
		}
}

void VariableStorage::initialize(int maxCharacters, int localSize)
{
		Q_UNUSED(maxCharacters)
		qDebug() << "[var] initialize localSize =" << localSize;
		// 脚本（重）装载后私有作用域的名字表可能变化 —— 整张作用域表作废。
		// 复位「已安装」标记：下一次装载收尾（EraParseTable::finalizeBindings ->
		// installPrivateScopeTable）会重新装；在那之前 setPrivateScope 退回按
		// 调用方名字建表（旧行为），不会出现「已安装但空表」的静默退化。
		m_privateNames.clear();
		m_privateTableInstalled = false;
		m_privateScopeId = eravar::kGlobalScope;
		if (localSize > 0) {
				m_localIntVars.fill(0, localSize);
				m_localStrVars.fill(FlatStringValue{}, localSize);
		}
}

// ================= 全局整型 3D 实现示例 =================
void VariableStorage::setGlobalInt3D(const QString &name, int x, int y, int z, qint64 val)
{
		setGlobalInt3DRaw(storageName(name), x, y, z, val);
}

qint64 VariableStorage::getGlobalInt3D(const QString &name, int x, int y, int z) const
{
		return getGlobalInt3DRaw(storageName(name), x, y, z);
}

// ================= 名字级整数访问（唯一实现） =================
// 维数来源：优先注入的 provider（ExpressionEvaluator::variableDimension，与旧
// ExecutionEngine::lhsDimension 同源）；未接线时退回本类的声明表。
int VariableStorage::intDimension(const QString &name) const
{
		if (m_intDimProvider) {
				const int d = m_intDimProvider(name);
				if (d >= 1 && d <= 3) return d;
		}
		if (isVariable3D(name)) return 3;
		if (isVariable2D(name)) return 2;
		return 1;
}

qint64 VariableStorage::readIntByName(const QString &name, const QList<int> &indices,
                                     eravar::ScopeId scope) const
{
		const int first = indices.value(0);
		if (hasParameter(name)) return parameter(name).toLongLong();
		// 名字 -> 存储键只解析一次（scope 是显式参数：私有名按该函数的名表解析）。
		const QString key = storageName(name, scope);
		// 角色数据变量：按 (角色号, 元素下标) 读取；字符串槽整型读恒 0
		if (isCharaDataVariable(name)) {
				if (isCharaDataString(name)) return 0;
				int charaId = 0;
				QList<int> elems;
				reduceCharaArgs(name, indices, charaId, elems);
				if (charaDataDimension(name) >= 2)
						return getCharaInt3D(key, charaId, elems.value(0), elems.value(1));
				return getCharaInt(key, charaId, elems.value(0));
		}
		const QString upper = eraUpperKey(name);
		if (upper == QLatin1String("ARG")) return getArgInt(first);
		if (upper == QLatin1String("LOCAL")) return getLocalInt(first);
		if (hasSystemVariable(name)) return getSystemVariable(name, first);
		// 多维访问：声明维数 >= 2 且给了 >= 2 个下标（旧 writeLhs 的同款判定）
		const int dim = intDimension(name);
		if (indices.size() >= 2 && dim >= 2) {
				if (indices.size() >= 3 && dim >= 3)
						return getGlobalInt3DRaw(key, indices.at(0), indices.at(1), indices.at(2));
				return getGlobalInt2DRaw(key, indices.at(0), indices.at(1));
		}
		return getGlobalInt1DRaw(key, first);
}

void VariableStorage::writeIntByName(const QString &name, const QList<int> &indices, qint64 value,
                                     eravar::ScopeId scope)
{
		const int first = indices.value(0);
		if (hasParameter(name)) { setParameter(name, value); return; }
		const QString key = storageName(name, scope);
		// 角色数据变量：按 (角色号, 元素下标) 写入（否则同一角色的元素互相覆盖）
		if (isCharaDataVariable(name)) {
				if (isCharaDataString(name)) return;   // 字符串：走 assignStringVariable
				int charaId = 0;
				QList<int> elems;
				reduceCharaArgs(name, indices, charaId, elems);
				if (charaDataDimension(name) >= 2)
						setCharaInt3D(key, charaId, elems.value(0), elems.value(1), value);
				else
						setCharaInt(key, charaId, elems.value(0), value);
				return;
		}
		const QString upper = eraUpperKey(name);
		if (upper == QLatin1String("ARG")) { setArgInt(first, value); return; }
		if (upper == QLatin1String("LOCAL")) { setLocalInt(first, value); return; }
		if (upper == QLatin1String("ARGS") || upper == QLatin1String("LOCALS")
		    || upper == QLatin1String("RESULTS")) {
				return;   // 字符串变量：走 assignStringVariable
		}
		if (hasSystemVariable(name)) { setSystemVariable(name, first, value); return; }
		const int dim = intDimension(name);
		if (indices.size() >= 2 && dim >= 2) {
				if (indices.size() >= 3 && dim >= 3) {
						setGlobalInt3DRaw(key, indices.at(0), indices.at(1), indices.at(2), value);
						return;
				}
				setGlobalInt2DRaw(key, indices.at(0), indices.at(1), value);
				return;
		}
		setGlobalInt1DRaw(key, first, value);
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
		vec[index] = FlatStringValue(value);
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
			return eravar::isPrivateStorageKey(k);
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
		purge(m_globalIntN);
		purge(m_globalStrN);
		qDebug() << "[global] RESETGLOBAL（保留函数私有变量）";
}

// ================= RESETDATA / 新开游戏 =================
// 把 ERB/ERH 里声明过的变量恢复成初值（C# SetDefaultValue + SetDefaultLocalValue）。
// RESETDATA 与读档（applyBinarySave 的 normal 分支）共用：FL3.8 有 18515 个声明、
// 合计约 400 万个元素，写法要克制：
//   · 存储键每个声明只解析一次（setGlobal*D 内部每次都走 storageName）
//   · 1 维走 memset 批量清零，只覆盖写了初值的前几项
//   · 2/3 维先按维数扩好表再按下标直写（coordinates() 每个元素都会分配一个
//     QList，400 万次分配在 -O0 下就要 0.7 秒）
//   · 声明列表走 declarationsRef()（每次重建是 18515 项的整表拷贝）
void VariableStorage::restoreDeclaredDefaults()
{
	if (!m_saveVariableTable) return;
	for (const VariableDecl& d : m_saveVariableTable->declarationsRef()) {
		if (d.isConst || d.isReference || d.isCharaData || d.isGlobal || d.isArg) continue;
		// 与 setPrivateScope / computeVariableAccess 同一个构造点（分隔符只在那里出现）。
		const QString key = d.scope == VarScope::Local
			? eravar::makePrivateStorageKey(d.scopeKey, d.name)
			: d.name.toUpper();
		const QString storageKey = resolvedStorageName(key);
		const QList<int> dims = d.lengths.isEmpty() ? QList<int>{1} : d.lengths;
		const int count = erasave::shapeCount(dims);
		const int defaultCount = d.type == OperandType::Str ? int(d.defaultStr.size())
		                                                    : int(d.defaultInt.size());
		if (d.dimension <= 1) {
			// 1 维：x 就是 i
			if (d.type == OperandType::Str) {
				auto& vec = m_globalStr1D[storageKey];
				if (vec.size() < count) vec.resize(count);
				std::fill_n(vec.begin(), count, FlatStringValue{});
				const int n = qMin(count, defaultCount);
				for (int i = 0; i < n; ++i) vec[i] = FlatStringValue(d.defaultStr.at(i));
			} else {
				auto& vec = m_globalInt1D[storageKey];
				if (vec.size() < count) vec.resize(count, 0);
				if (count > 0) std::memset(vec.data(), 0, size_t(count) * sizeof(qint64));
				const int n = qMin(count, defaultCount);
				for (int i = 0; i < n; ++i) vec[i] = d.defaultInt.at(i);
			}
			continue;
		}
		// 2/3 维：走独立的多维扁平容器（装载期按 lengths 乘积分配）。
		// 形状来自声明；这里不再按嵌套 QList 扩容。未安装（测试/无装载 pass）
		// 时就地安装一次，保持「恢复初值」在任何入口都成立。
		if (!isMultiDimArray(storageKey))
			installMultiDimArray(storageKey, dims, d.strides, d.type == OperandType::Str);
		const auto shapeIt = m_multiDimShapes.constFind(storageKey);
		if (shapeIt == m_multiDimShapes.constEnd() || !shapeIt.value().allocated) continue;
		if (d.type == OperandType::Str) {
			auto& vec = m_globalStrN[storageKey];
			if (vec.size() < count) vec.resize(count);
			std::fill_n(vec.begin(), count, FlatStringValue{});
			const int n = qMin(count, defaultCount);
			for (int i = 0; i < n; ++i) vec[i] = FlatStringValue(d.defaultStr.at(i));
		} else {
			auto& vec = m_globalIntN[storageKey];
			if (vec.size() < count) vec.resize(count, 0);
			if (count > 0) std::memset(vec.data(), 0, size_t(count) * sizeof(qint64));
			const int n = qMin(count, defaultCount);
			for (int i = 0; i < n; ++i) vec[i] = d.defaultInt.at(i);
		}
	}
}

void VariableStorage::resetForNewGame()
{
		// 对齐 C# VariableEvaluator.ResetData：SetDefaultValue(全部变量) + CharacterList.Clear()。
		// 保留：NAME 表、STR 等 CSV 装载期「默认值」数据、CSV 模板快照（ADDCHARA 重新
		// 填充角色时仍要用）；清除：全部运行时数值（内建整型数组、用户广域、角色运行时
		// 数据、本地槽）。
		// C# ResetData deliberately keeps GLOBAL/GLOBALS alive. Preserve both
		// built-ins and declared #GLOBAL variables while clearing ordinary data.
		const QSet<QString> globalNames = [&] {
			QSet<QString> names{QStringLiteral("GLOBAL"), QStringLiteral("GLOBALS")};
			if (m_saveVariableTable) for (const VariableDecl& d : m_saveVariableTable->declarationsRef())
				if (d.isGlobal) names.insert(d.name.toUpper());
			return names;
		}();
		const auto keep = [&](const auto& map) {
			auto result = map;
			for (auto it = result.begin(); it != result.end(); )
				if (!globalNames.contains(it.key().toUpper())) it = result.erase(it); else ++it;
			return result;
		};
		const auto keptI1 = keep(m_globalInt1D);
		const auto keptI2 = keep(m_globalInt2D);
		const auto keptI3 = keep(m_globalInt3D);
		const auto keptS1 = keep(m_globalStr1D);
		const auto keptS2 = keep(m_globalStr2D);
		const auto keptS3 = keep(m_globalStr3D);
		const auto keptIN = keep(m_globalIntN);
		const auto keptSN = keep(m_globalStrN);
		clearCharaList();
		resetGlobals();   // 用户普通广域变量回默认；GLOBAL/GLOBALS 稍后恢复
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
				if (eravar::isPrivateStorageKey(it.key())) it = map.erase(it);
				else ++it;
			}
		};
		purgePrivate(m_globalInt1D);
		purgePrivate(m_globalInt2D);
		purgePrivate(m_globalInt3D);
		purgePrivate(m_globalStr1D);
		purgePrivate(m_globalStr2D);
		purgePrivate(m_globalStr3D);
		purgePrivate(m_globalIntN);
		purgePrivate(m_globalStrN);
		// 私有名表是**装载期元数据**（脚本声明的一部分），不是每局数据：
		// RESETDATA 不能清掉它 —— 安装后 setPrivateScope 已不再建表，清掉就会让
		// 所有私有名静默退化成裸名（又是另一个变量）。未安装（测试手工路径）时
		// 保持旧行为清空。
		if (!m_privateTableInstalled) m_privateNames.clear();
		m_privateScopeId = eravar::kGlobalScope;

		restoreDeclaredDefaults();
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
		const auto restore = [](auto& map, const auto& saved) {
			for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) map.insert(it.key(), it.value());
		};
		restore(m_globalInt1D, keptI1); restore(m_globalInt2D, keptI2); restore(m_globalInt3D, keptI3);
		restore(m_globalStr1D, keptS1); restore(m_globalStr2D, keptS2); restore(m_globalStr3D, keptS3);
		restore(m_globalIntN, keptIN); restore(m_globalStrN, keptSN);
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
		m_localStrVars[index] = FlatStringValue(value);
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
		m_argStrVars[index] = FlatStringValue(value);
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

void VariableStorage::setItemprice(int index, qint64 value) { if (index >= 0 && index < m_itemprice.size()) m_itemprice[index] = value; }
qint64 VariableStorage::getItemprice(int index) const { return (index >= 0 && index < m_itemprice.size()) ? m_itemprice.at(index) : 0; }

void VariableStorage::resetItemprice()
{
		const int n = m_variableConfig.getSize1D(QStringLiteral("ITEMPRICE"));
		m_itemprice.assign(n > 0 ? n : 0, 0);
}

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
		setGlobalInt1DRaw(storageName(name), x, val);
}

void VariableStorage::setGlobalStr1D(const QString &name, int x, const QString &value)
{
		if (x < 0) return;
		const QString key = storageName(name);
		auto it = m_globalStr1D.find(key);
		if (it != m_globalStr1D.end()) {
				if (it.value().size() <= x) it.value().resize(x + 1);
				it.value()[x] = FlatStringValue(value);
				return;
		}
		if (isMultiDimArray(key)) { writeMultiDimStr(key, {x}, FlatStringValue(value)); return; }
		auto &vec = m_globalStr1D[key];
		if (vec.size() <= x) vec.resize(x + 1);
		vec[x] = FlatStringValue(value);
}

QString VariableStorage::getGlobalStr1D(const QString &name, int x) const
{
        return getGlobalStr1DRaw(storageName(name), x);
}

// VariableKey -> 存储键串：**唯一**的 id 解析入口。
// 热路径：一次 quint64 哈希（(scope<<32)|name）；未命中才做一次拼接并记忆化。
// 存储层仍是 QHash<QString,...>（存档格式逐字节不变），冷路径字符串只在这里产生。
const QString& VariableStorage::storageKeyFor(const eravar::VariableKey& key) const
{
        const quint64 packed = key.packed();
        const auto it = m_storageKeyCache.constFind(packed);
        if (it != m_storageKeyCache.constEnd()) return m_storageKeyStrings[it.value()];
        m_storageKeyStrings.push_back(eravar::storageKeyOf(key));
        m_storageKeyCache.insert(packed, quint32(m_storageKeyStrings.size()) - 1);
        return m_storageKeyStrings.back();
}

QString VariableStorage::getGlobalStr1DRaw(const QString& storageKey, int x) const
{
        if (x < 0) return QString();
        auto it = m_globalStr1D.constFind(storageKey);
        if (it != m_globalStr1D.constEnd() && x < it.value().size()) return it.value().at(x).text;
        if (isMultiDimArray(storageKey)) return readMultiDimStr(storageKey, {x}).text;
        return QString();
}

void VariableStorage::setGlobalStr2D(const QString &name, int x, int y, const QString &value)
{
		if (x < 0 || y < 0) return;
		const QString key = storageName(name);
		if (isMultiDimArray(key)) { writeMultiDimStr(key, {x, y}, FlatStringValue(value)); return; }
		auto &table = m_globalStr2D[key];
		if (table.size() <= x) table.resize(x + 1);
		if (table[x].size() <= y) table[x].resize(y + 1);
		table[x][y] = FlatStringValue(value);
}

QString VariableStorage::getGlobalStr2D(const QString &name, int x, int y) const
{
		if (x < 0 || y < 0) return QString();
		if (isMultiDimArray(storageName(name))) return getGlobalStr2DRaw(storageName(name), x, y);
		auto it = m_globalStr2D.constFind(storageName(name));
		if (it == m_globalStr2D.constEnd()) return QString();
		if (x >= it.value().size() || y >= it.value().at(x).size()) return QString();
		return it.value().at(x).at(y).text;
}

void VariableStorage::setSystemStr(const QString &name, int index, const QString &value)
{
		if (index != 0) return;   // 系统字符串变量目前只有单值（RESULTS / SAVEDATA_TEXT）
		m_systemStrVars[eraUpperKey(name)] = FlatStringValue(value);
}

QString VariableStorage::getSystemStr(const QString &name, int index) const
{
		if (index != 0) return QString();
		return m_systemStrVars.value(eraUpperKey(name)).text;
}

qint64 VariableStorage::getGlobalInt1D(const QString &name, int x) const
{
		return getGlobalInt1DRaw(storageName(name), x);
}

// ================= Raw 访问族（键已解析；见 variable_storage.h 说明） =================
qint64 VariableStorage::getGlobalInt1DRaw(const QString& storageKey, int x) const
{
		if (x < 0) return 0;
		const auto it = m_globalInt1D.constFind(storageKey);
		if (it != m_globalInt1D.constEnd() && x < it.value().size()) return it.value().at(x);
		// 多维变量的「扁平写法」：与 X:i:j:k 共用同一个扁平容器（脚本层由
		// 静态检查要求给足下标；这里是内部/旧路径的统一口径）。
		if (isMultiDimArray(storageKey)) return readMultiDimInt(storageKey, {x});
		return 0;
}

qint64 VariableStorage::getGlobalInt2DRaw(const QString& storageKey, int x, int y) const
{
		if (x < 0 || y < 0) return 0;
		if (isMultiDimArray(storageKey)) return readMultiDimInt(storageKey, {x, y});
		const auto it = m_globalInt2D.constFind(storageKey);
		if (it != m_globalInt2D.constEnd() && x < it.value().size()) {
				const auto& vec = it.value().at(x);
				if (y < vec.size()) return vec.at(y);
		}
		return 0;
}

qint64 VariableStorage::getGlobalInt3DRaw(const QString& storageKey, int x, int y, int z) const
{
		if (x < 0 || y < 0 || z < 0) return 0;
		if (isMultiDimArray(storageKey)) return readMultiDimInt(storageKey, {x, y, z});
		const auto it = m_globalInt3D.constFind(storageKey);
		if (it != m_globalInt3D.constEnd() && x < it.value().size()) {
				const auto& plane = it.value().at(x);
				if (y < plane.size()) {
						const auto& vec = plane.at(y);
						if (z < vec.size()) return vec.at(z);
				}
		}
		return 0;
}

QString VariableStorage::getGlobalStr2DRaw(const QString& storageKey, int x, int y) const
{
		if (x < 0 || y < 0) return QString();
		if (isMultiDimArray(storageKey)) return readMultiDimStr(storageKey, {x, y}).text;
		const auto it = m_globalStr2D.constFind(storageKey);
		if (it != m_globalStr2D.constEnd() && x < it.value().size()) {
				const auto& vec = it.value().at(x);
				if (y < vec.size()) return vec.at(y).text;
		}
		return QString();
}

QString VariableStorage::getGlobalStr3DRaw(const QString& storageKey, int x, int y, int z) const
{
		if (x < 0 || y < 0 || z < 0) return QString();
		if (isMultiDimArray(storageKey)) return readMultiDimStr(storageKey, {x, y, z}).text;
		const auto it = m_globalStr3D.constFind(storageKey);
		if (it != m_globalStr3D.constEnd() && x < it.value().size()) {
				const auto& plane = it.value().at(x);
				if (y < plane.size()) {
						const auto& vec = plane.at(y);
						if (z < vec.size()) return vec.at(z).text;
				}
		}
		return QString();
}

void VariableStorage::setGlobalInt1DRaw(const QString& storageKey, int x, qint64 value)
{
		if (x < 0) return;
		auto it = m_globalInt1D.find(storageKey);
		if (it != m_globalInt1D.end()) {
				if (it.value().size() <= x) it.value().resize(x + 1, 0);
				it.value()[x] = value;
				return;
		}
		if (isMultiDimArray(storageKey)) { writeMultiDimInt(storageKey, {x}, value); return; }
		auto& vec = m_globalInt1D[storageKey];
		if (vec.size() <= x) vec.resize(x + 1, 0);
		vec[x] = value;
}

void VariableStorage::setGlobalInt2DRaw(const QString& storageKey, int x, int y, qint64 value)
{
		if (x < 0 || y < 0) return;
		if (isMultiDimArray(storageKey)) { writeMultiDimInt(storageKey, {x, y}, value); return; }
		auto& grid2D = m_globalInt2D[storageKey];
		if (grid2D.size() <= x) grid2D.resize(x + 1);
		auto& vec = grid2D[x];
		if (vec.size() <= y) vec.resize(y + 1, 0);
		vec[y] = value;
}

void VariableStorage::setGlobalInt3DRaw(const QString& storageKey, int x, int y, int z, qint64 value)
{
		if (x < 0 || y < 0 || z < 0) return;
		if (isMultiDimArray(storageKey)) { writeMultiDimInt(storageKey, {x, y, z}, value); return; }
		auto& cube = m_globalInt3D[storageKey];
		if (cube.size() <= x) cube.resize(x + 1);
		auto& plane = cube[x];
		if (plane.size() <= y) plane.resize(y + 1);
		auto& vec = plane[y];
		if (vec.size() <= z) vec.resize(z + 1, 0);
		vec[z] = value;
}

void VariableStorage::setGlobalInt2D(const QString &name, int x, int y, qint64 val)
{
		setGlobalInt2DRaw(storageName(name), x, y, val);
}

qint64 VariableStorage::getGlobalInt2D(const QString &name, int x, int y) const
{
		if (x < 0 || y < 0) return 0;
		if (isMultiDimArray(storageName(name))) return getGlobalInt2DRaw(storageName(name), x, y);
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
		// 「有存储通道的系统变量」名单与装载期变量分词共用同一份
		//（sysvar::systemStorageNames —— **新增系统变量必须同步那里**）：
		// 漏在这里会让运行期把它当成用户全局变量（退回 m_globalInt1D）——
		// ITEMPRICE 就是这样变成恒 0 的（装载期写常量容器、读取走用户全局槽）。
		// 先经 REF 形参别名解析（`#DIM REF X` ← 系统变量实参），再判名。
		return sysvar::systemStorageNames().contains(systemVariableName(name));
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
		// 常量：Item.csv 第 3 列（C# Item1DConstantToken）
		if (key == "ITEMPRICE") return getItemprice(index);
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
		// 内部装载路径（ConstantData.loadDataTo）；ERB 侧 ITEMPRICE 是 readOnly 常量，
		// 赋值在解析期就被 not-const 校验拦下。
		if (key == "ITEMPRICE") { setItemprice(index, value); return; }
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
// 存档单元的逐元素形状换算（最后一维连续，与旧的 coordinates() 一致）。
// 这两个模板只负责「按下标取一格」的循环；格子从哪来由各 Slot 分支绑定。
template <typename Cell>
void appendInts(EraBinaryData::Variable& v, Cell&& cell)
{
    const QList<int>& dims = v.dimensions;
    const int axes = dims.size();
    const int d0 = qMax(1, dims.value(0)), d1 = qMax(1, dims.value(1)), d2 = qMax(1, dims.value(2));
    const int n = erasave::shapeCount(dims);
    for (int i = 0; i < n; ++i) {
        int x = 0, y = 0, z = 0;
        if (axes <= 1) x = i % d0;               // 标量（rank 0）只有 i = 0
        else if (axes == 2) { x = (i / d1) % d0; y = i % d1; }
        else { z = i % d2; const int t = i / d2; y = t % d1; x = t / d1; }
        v.integers.append(cell(x, y, z));
    }
}
template <typename Cell>
void appendStrings(EraBinaryData::Variable& v, Cell&& cell)
{
    const QList<int>& dims = v.dimensions;
    const int axes = dims.size();
    const int d0 = qMax(1, dims.value(0)), d1 = qMax(1, dims.value(1)), d2 = qMax(1, dims.value(2));
    const int n = erasave::shapeCount(dims);
    for (int i = 0; i < n; ++i) {
        int x = 0, y = 0, z = 0;
        if (axes <= 1) x = i % d0;
        else if (axes == 2) { x = (i / d1) % d0; y = i % d1; }
        else { z = i % d2; const int t = i / d2; y = t % d1; x = t / d1; }
        v.strings.append(cell(x, y, z));
    }
}
} // namespace

// 存档映射层的存储侧：一个条目 -> 一个存档变量。
// 键一律走 storageKeyFor(VariableKey)（id -> 存储键的唯一入口），不再由
// 档案代码按名字重新解析作用域/大小写/REF。
EraBinaryData::Variable VariableStorage::readSaveEntry(const erasave::Entry& entry, int charaSlot) const
{
    EraBinaryData::Variable v;
    v.name = entry.name;
    v.string = entry.string;
    v.dimensions = entry.dimensions;
    const QString& key = storageKeyFor(entry.key);
    switch (entry.slot) {
    case erasave::Slot::SystemInt1D:
        appendInts(v, [&](int x, int, int) { return getSystemVariable(entry.name, x); });
        break;
    case erasave::Slot::GlobalInt1D: {
        const auto it = m_globalInt1D.constFind(key);
        appendInts(v, [&](int x, int, int) -> qint64 {
            return it != m_globalInt1D.constEnd() && x < it.value().size() ? it.value().at(x) : 0; });
        break; }
    case erasave::Slot::GlobalInt2D:
        appendInts(v, [&](int x, int y, int) { return getGlobalInt2DRaw(key, x, y); });
        break;
    case erasave::Slot::GlobalInt3D:
        appendInts(v, [&](int x, int y, int z) { return getGlobalInt3DRaw(key, x, y, z); });
        break;
    case erasave::Slot::GlobalStr1D:
        appendStrings(v, [&](int x, int, int) { return getGlobalStr1DRaw(key, x); });
        break;
    case erasave::Slot::GlobalStr2D:
        appendStrings(v, [&](int x, int y, int) { return getGlobalStr2DRaw(key, x, y); });
        break;
    case erasave::Slot::GlobalStr3D:
        appendStrings(v, [&](int x, int y, int z) { return getGlobalStr3DRaw(key, x, y, z); });
        break;
    case erasave::Slot::CharaInt1D: {
        const auto it = m_charaIntVars.constFind(key);
        appendInts(v, [&](int x, int, int) -> qint64 {
            return it != m_charaIntVars.constEnd() && charaSlot >= 0 && charaSlot < it.value().size()
                    && x < it.value().at(charaSlot).size() ? it.value().at(charaSlot).at(x) : 0; });
        break; }
    case erasave::Slot::CharaInt2D: {
        const auto it = m_charaIntVars3D.constFind(key);
        appendInts(v, [&](int x, int y, int) -> qint64 {
            return it != m_charaIntVars3D.constEnd() && charaSlot >= 0 && charaSlot < it.value().size()
                    && x < it.value().at(charaSlot).size() && y < it.value().at(charaSlot).at(x).size()
                ? it.value().at(charaSlot).at(x).at(y) : 0; });
        break; }
    case erasave::Slot::CharaStr1D: {
        const auto it = m_charaStrVars.constFind(key);
        appendStrings(v, [&](int x, int, int) -> QString {
            return it != m_charaStrVars.constEnd() && charaSlot >= 0 && charaSlot < it.value().size()
                    && x < it.value().at(charaSlot).size() ? it.value().at(charaSlot).at(x).text : QString(); });
        break; }
    case erasave::Slot::CharaStr2D: {
        const auto it = m_charaStr2D.constFind(key);
        appendStrings(v, [&](int x, int y, int) -> QString {
            return it != m_charaStr2D.constEnd() && charaSlot >= 0 && charaSlot < it.value().size()
                    && x < it.value().at(charaSlot).size() && y < it.value().at(charaSlot).at(x).size()
                ? it.value().at(charaSlot).at(x).at(y).text : QString(); });
        break; }
    }
    return v;
}

void VariableStorage::writeSaveEntry(const erasave::Entry& entry, int charaSlot, int x, int y, int z,
                                     qint64 number, const QString& text)
{
    const QString& key = storageKeyFor(entry.key);
    const auto put1 = [](auto& list, int i, const auto& value) {
        if (list.size() <= i) list.resize(i + 1);
        list[i] = value;
    };
    switch (entry.slot) {
    case erasave::Slot::SystemInt1D: setSystemVariable(entry.name, x, number); break;
    case erasave::Slot::GlobalInt1D: put1(m_globalInt1D[key], x, number); break;
    case erasave::Slot::GlobalStr1D: put1(m_globalStr1D[key], x, FlatStringValue(text)); break;
    case erasave::Slot::GlobalInt2D:
        if (isMultiDimArray(key)) writeMultiDimInt(key, {x, y}, number);
        else { auto& a = m_globalInt2D[key]; if (a.size() <= x) a.resize(x + 1);
               put1(a[x], y, number); }
        break;
    case erasave::Slot::GlobalStr2D:
        if (isMultiDimArray(key)) writeMultiDimStr(key, {x, y}, FlatStringValue(text));
        else { auto& a = m_globalStr2D[key]; if (a.size() <= x) a.resize(x + 1);
               put1(a[x], y, FlatStringValue(text)); }
        break;
    case erasave::Slot::GlobalInt3D:
        if (isMultiDimArray(key)) writeMultiDimInt(key, {x, y, z}, number);
        else { auto& a = m_globalInt3D[key];
               if (a.size() <= x) a.resize(x + 1);
               if (a[x].size() <= y) a[x].resize(y + 1);
               put1(a[x][y], z, number); }
        break;
    case erasave::Slot::GlobalStr3D:
        if (isMultiDimArray(key)) writeMultiDimStr(key, {x, y, z}, FlatStringValue(text));
        else { auto& a = m_globalStr3D[key];
               if (a.size() <= x) a.resize(x + 1);
               if (a[x].size() <= y) a[x].resize(y + 1);
               put1(a[x][y], z, FlatStringValue(text)); }
        break;
    case erasave::Slot::CharaInt1D:
        if (charaSlot >= 0) { auto& a = m_charaIntVars[key]; if (a.size() <= charaSlot) a.resize(charaSlot + 1);
                              put1(a[charaSlot], x, number); }
        break;
    case erasave::Slot::CharaStr1D:
        if (charaSlot >= 0) { auto& a = m_charaStrVars[key]; if (a.size() <= charaSlot) a.resize(charaSlot + 1);
                              put1(a[charaSlot], x, FlatStringValue(text)); }
        break;
    case erasave::Slot::CharaInt2D:
        if (charaSlot >= 0) { auto& a = m_charaIntVars3D[key]; if (a.size() <= charaSlot) a.resize(charaSlot + 1);
                              if (a[charaSlot].size() <= x) a[charaSlot].resize(x + 1);
                              put1(a[charaSlot][x], y, number); }
        break;
    case erasave::Slot::CharaStr2D:
        if (charaSlot >= 0) { auto& a = m_charaStr2D[key]; if (a.size() <= charaSlot) a.resize(charaSlot + 1);
                              if (a[charaSlot].size() <= x) a[charaSlot].resize(x + 1);
                              put1(a[charaSlot][x], y, FlatStringValue(text)); }
        break;
    }
}

VariableStorage::SaveReadiness VariableStorage::saveReadiness() const {
    if (m_saveVariableTable == nullptr) return SaveReadiness::NoDeclarationTable;
    if (!m_saveVariableTable->dimensionsResolved()) return SaveReadiness::DeclarationsNotResolved;
    return SaveReadiness::Ready;
}

bool VariableStorage::checkSaveReadiness(const char* operation) const {
    switch (saveReadiness()) {
    case SaveReadiness::Ready:
        return true;
    case SaveReadiness::NoDeclarationTable:
        qWarning().noquote() << "[save]" << operation
                             << "：没有接上声明表 —— 存档只含内建条目（用户 SAVEDATA 变量不会写入）";
        return false;
    case SaveReadiness::DeclarationsNotResolved:
        qWarning().noquote() << "[save]" << operation
                             << "：声明尚未定稿（resolveDimensions 未跑）—— 存档形状可能按声明文本猜";
        return false;
    }
    return false;
}

EraBinaryData::Document VariableStorage::captureBinarySave(EraBinaryData::FileType type,
                                                           const QList<int>& selected) const {
    auto* self=const_cast<VariableStorage*>(this);
    const auto context=localContext(); self->setLocalContext({});
    EraBinaryData::Document document; document.type=type;
    // 存档条目表（名字 / 身份 / 容器 / 形状）由声明构建；这里不再认识键的形态。
    const erasave::Map map=erasave::Map::build(m_variableConfig,m_saveVariableTable,type);
    const erasave::Map charaMap=(type==EraBinaryData::FileType::Normal || type==EraBinaryData::FileType::Character)
        ? erasave::Map::build(m_variableConfig,m_saveVariableTable,EraBinaryData::FileType::Character)
        : erasave::Map{};
    if(type==EraBinaryData::FileType::Normal || type==EraBinaryData::FileType::Character) {
        QList<int> indexes=selected;
        if(indexes.isEmpty()) for(int i=0;i<charaNum();++i) indexes.append(i);
        for(int index : indexes) {
            if(index<0 || index>=charaNum()) continue;
            EraBinaryData::Character c;
            for(const auto& e:charaMap.entries()) (e.user ? c.userDefined : c.builtins).append(readSaveEntry(e,resolveCharaIndex(index)));
            document.characters.append(c);
        }
    }
    if(type!=EraBinaryData::FileType::Character)
        for(const auto& e:map.entries()) document.variables.append(readSaveEntry(e,-1));
    self->setLocalContext(context);
    return document;
}

void VariableStorage::applyBinarySave(const EraBinaryData::Document& document) {
    const bool normal=document.type==EraBinaryData::FileType::Normal;
    // GLOBAL is a separate lifetime and does not belong to a normal load.
    const auto globals=normal ? captureBinarySave(EraBinaryData::FileType::Global) : EraBinaryData::Document{};
    // Keep every GLOBAL declaration, including tokens that are deliberately not SAVEDATA.
    QSet<QString> globalKeys{QStringLiteral("GLOBAL"), QStringLiteral("GLOBALS")};
    if (m_saveVariableTable) for (const auto& d : m_saveVariableTable->declarationsRef())
        if (d.isGlobal) globalKeys.insert(d.name.toUpper());
    const auto preserve = [&](const auto& map) {
        auto kept = map;
        for (auto it = kept.begin(); it != kept.end(); )
            if (!globalKeys.contains(it.key().toUpper())) it = kept.erase(it); else ++it;
        return kept;
    };
    const auto keptI1=preserve(m_globalInt1D); const auto keptI2=preserve(m_globalInt2D); const auto keptI3=preserve(m_globalInt3D);
    const auto keptS1=preserve(m_globalStr1D); const auto keptS2=preserve(m_globalStr2D); const auto keptS3=preserve(m_globalStr3D);
    const auto keptIN=preserve(m_globalIntN); const auto keptSN=preserve(m_globalStrN);
    if(normal) {
        resetForNewGame(); setLocalContext({});
        // 声明初值恢复 = resetForNewGame 的同一件事，走同一个批量实现
        //（旧代码在这里还有一份逐元素 setGlobal*D + coordinates() 的手写副本，
        // 400 万个元素读一次档付一次）。
        restoreDeclaredDefaults();
        applyBinarySave(globals);
        const auto restore=[](auto& map,const auto& kept) { for(auto it=kept.begin();it!=kept.end();++it) map.insert(it.key(),it.value()); };
        restore(m_globalInt1D,keptI1); restore(m_globalInt2D,keptI2); restore(m_globalInt3D,keptI3);
        restore(m_globalStr1D,keptS1); restore(m_globalStr2D,keptS2); restore(m_globalStr3D,keptS3);
        restore(m_globalIntN,keptIN); restore(m_globalStrN,keptSN);
    }
    const erasave::Map charaMap=erasave::Map::build(m_variableConfig,m_saveVariableTable,EraBinaryData::FileType::Character);
    const erasave::Map variableMap=erasave::Map::build(m_variableConfig,m_saveVariableTable,
        document.type==EraBinaryData::FileType::Global ? EraBinaryData::FileType::Global
        : document.type==EraBinaryData::FileType::Var ? EraBinaryData::FileType::Var
                                                      : EraBinaryData::FileType::Normal);
    const auto apply = [&](const EraBinaryData::Variable& v,const erasave::Entry& e,int character) {
        if(v.string!=e.string || v.dimensions.size()!=e.dimensions.size()) return;
        // 下标换算与 coordinates() 一致（最后一维连续），但不再为每个元素分配
        // QList；source 的轴序混合（v.dimensions 可能小于目标 dims，读不到的
        // 位置按 0/空串补）也一并内联。
        const QList<int>& dims=e.dimensions;
        const int axisCount=dims.size();
        // rank-0 形状（NAME/CALLNAME/NICKNAME/MASTERNAME/ISASSI/NO 的 dims 为空）：
        // 语义 = 单元素、下标全 0（shapeCount({}) 也返回 1）。
        int vstride0=1,vstride1=1;
        for(int axis=axisCount-2;axis>=0;--axis) {
            int vacc=1; for(int k=axis+1;k<axisCount;++k) vacc*=qMax(1,v.dimensions[k]);
            if(axis==0) vstride0=vacc; else vstride1=vacc;
        }
        const int d0=qMax(1,dims.value(0)), d1=qMax(1,dims.value(1)), d2=qMax(1,dims.value(2));
        const int count=axisCount==0 ? 1 : erasave::shapeCount(dims);
        const int slot=character>=0 ? resolveCharaIndex(character) : -1;
        for(int i=0;i<count;++i) {
            int x=0,y=0,z=0,source=0; bool overlap=true;
            if(axisCount==0) {
                // rank-0：x=y=z=source=0（overlap 恒真）
            } else if(axisCount==1) {
                x=i%d0;
                if(x>=v.dimensions.value(0)) overlap=false;
                source=x;
            } else if(axisCount==2) {
                x=(i/d1)%d0; y=i%d1;
                if(x>=v.dimensions.value(0)) overlap=false;
                if(y>=v.dimensions.value(1)) overlap=false;
                source=x*vstride0+y;
            } else {
                z=i%d2; const int t=i/d2; y=t%d1; x=t/d1;
                if(x>=v.dimensions.value(0)) overlap=false;
                if(y>=v.dimensions.value(1)) overlap=false;
                if(z>=v.dimensions.value(2)) overlap=false;
                source=x*vstride0+y*vstride1+z;
            }
            const qint64 number=overlap ? v.integers.value(source) : 0;
            const QString text=overlap ? v.strings.value(source) : QString();
            writeSaveEntry(e,slot,x,y,z,number,text);
        }
    };
    const auto applyList = [&](const QList<EraBinaryData::Variable>& list,const erasave::Map& map,int character) {
        for(const auto& v:list) if(const erasave::Entry* e=map.findByName(v.name)) apply(v,*e,character);
    };
    if(document.type==EraBinaryData::FileType::Normal || document.type==EraBinaryData::FileType::Character) {
        for(const auto& c:document.characters) {
            // Runtime slots must be distinct even when NO is repeated or the original list was reordered.
            int slot=0; for(int n:m_charaList) slot=qMax(slot,n+1);
            const int index=m_charaList.size(); m_charaList.append(slot);
            for(const auto& e:charaMap.entries()) {
                EraBinaryData::Variable zero; zero.name=e.name; zero.string=e.string; zero.dimensions=e.dimensions;
                if(e.string) zero.strings.resize(erasave::shapeCount(e.dimensions));
                else zero.integers.resize(erasave::shapeCount(e.dimensions));
                apply(zero,e,index);
            }
            applyList(c.builtins,charaMap,index); applyList(c.userDefined,charaMap,index);
        }
    }
    applyList(document.variables,variableMap,-1);
}

FlatStringValue VariableStorage::getStringSlot(const QString& name, const QList<int>& indices) const {
    return getStringSlot(name, indices, m_privateScopeId);
}

FlatStringValue VariableStorage::getStringSlot(const QString& name, const QList<int>& indices,
                                               eravar::ScopeId scope) const {
    const QString key = storageName(name, scope);
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
    if (isMultiDimArray(key)) return readMultiDimStr(key, indices);
    if (indices.size() >= 3) return m_globalStr3D.value(key).value(x).value(y).value(z);
    if (indices.size() >= 2) return m_globalStr2D.value(key).value(x).value(y);
    return m_globalStr1D.value(key).value(x);
}

// 键已解析的 getStringSlot（「变量分词」快路径用）：跳过 storageName，其余分派
// 与 getStringSlot 一致。**必须返回 FlatStringValue（带 markup 分片）** —— HTML
// 累积（html += HTML_BUTTON(...)）与打印模板都靠 markup，不能只回裸文本。
// 角色/系统字符串名（CSTR/SAVEDATA_TEXT…）在分词期已判 Dynamic，不进这里。
FlatStringValue VariableStorage::getStringSlotRaw(const QString& storageKey, const QList<int>& indices) const {
    const int x = indices.value(0), y = indices.value(1), z = indices.value(2);
    if (x < 0 || y < 0 || z < 0) return {};
    if (storageKey == QLatin1String("LOCALS")) return m_localStrVars.value(x);
    if (storageKey == QLatin1String("ARGS")) return m_argStrVars.value(x);
    if (storageKey == QLatin1String("SAVEDATA_TEXT")) return m_systemStrVars.value(storageKey);
    if (isMultiDimArray(storageKey)) return readMultiDimStr(storageKey, indices);
    if (indices.size() >= 3) return m_globalStr3D.value(storageKey).value(x).value(y).value(z);
    if (indices.size() >= 2) return m_globalStr2D.value(storageKey).value(x).value(y);
    return m_globalStr1D.value(storageKey).value(x);
}

bool VariableStorage::setStringSlot(const QString& name, const QList<int>& indices, const FlatStringValue& value) {
    return setStringSlot(name, indices, value, m_privateScopeId);
}

bool VariableStorage::setStringSlot(const QString& name, const QList<int>& indices,
                                    const FlatStringValue& value, eravar::ScopeId scope) {
    const QString key = storageName(name, scope);
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
    else if (isMultiDimArray(key)) writeMultiDimStr(key, indices, value);
    else if (indices.size() >= 3) put3(m_globalStr3D[key], x, y, z);
    else if (indices.size() >= 2) put2(m_globalStr2D[key], x, y);
    else put1(m_globalStr1D[key], x);
    return true;
}
