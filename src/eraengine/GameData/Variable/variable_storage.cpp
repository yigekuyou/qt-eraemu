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
#include "ast/name_interner.h"

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
// 扁平槽位序列（唯一的容器形态）
//
// 每个存储键 = 一条扁平 QList；维数不决定容器，只决定取址几何：
//     offset = Σ idx[d] · strides[d]        （addressOf，唯一的取址公式）
// CHARADATA 在同一布局上加角色 stride：
//     slot   = charaSlot · elementCount + offset
// 形状仍只由 shapeOf 回答；容器里的几何缓存（m_geometries / m_charaGeometries）
// 只有 installGeometry 一个写入点，缓存的是**同一份**事实（strides 是 shapeOf
// 不提供的、每次取址都需要的量）。
// ---------------------------------------------------------------------------

void VariableStorage::installGeometry(QHash<QString, SlotGeometry>& map,
                                      const QString& storageKey,
                                      const QList<int>& lengths, const QList<int>& strides,
                                      qint64 elementCount, bool allocated, bool authoritative)
{
    if (storageKey.isEmpty()) return;
    SlotGeometry geometry;
    geometry.lengths = lengths;
    geometry.elementCount = elementCount > 0 ? elementCount : 1;   // 标量也占一格
    geometry.allocated = allocated;
    geometry.authoritative = authoritative;
    if (strides.size() == lengths.size()) {
        geometry.strides = strides;
    } else {
        // 兜底：调用方没给 strides 时按「最后一维连续」补一次。
        geometry.strides.resize(lengths.size());
        qint64 acc = 1;
        for (int i = lengths.size() - 1; i >= 0; --i) {
            geometry.strides[i] = int(acc);
            acc *= lengths.at(i);
        }
    }
    map.insert(storageKey, geometry);
}

qint64 VariableStorage::addressOf(const SlotGeometry* geometry, const QList<int>& indices)
{
    if (geometry == nullptr) {
        // 无几何：只有 1D 语义（扁平序号），容器按需增长。多维坐标无法取址。
        if (indices.size() >= 2) return -1;
        return indices.isEmpty() ? 0 : qint64(indices.value(0));
    }
    if (indices.isEmpty()) return geometry->elementCount > 0 ? 0 : -1;
    if (indices.size() == 1) {
        // 扁平写法：X[flat]（与 X:i:j:k 共用同一条序列）。
        const qint64 flat = indices.value(0);
        return (flat >= 0 && flat < geometry->elementCount) ? flat : -1;
    }
    const int rank = geometry->lengths.size();
    if (indices.size() > rank) return -1;                 // 下标个数超过维数
    qint64 offset = 0;
    for (int d = 0; d < indices.size(); ++d) {
        const int index = indices.at(d);
        if (index < 0 || index >= geometry->lengths.at(d)) return -1;
        offset += qint64(index) * geometry->strides.at(d);   // 唯一的取址公式
    }
    if (offset < 0 || offset >= geometry->elementCount) return -1;
    return offset;
}

// 唯一的角色 stride 公式：角色块基址 = 角色号 · 每角色元素数。
qint64 VariableStorage::roleSlotOf(const SlotGeometry& geometry, int charaSlot,
                                   qint64 elementOffset)
{
    const qint64 stride = geometry.elementCount > 0 ? geometry.elementCount : 1;
    return qint64(charaSlot) * stride + elementOffset;
}

// 外接几何（运行期才长出来的数组）扩容：把边界扩到包含 indices，并按新 strides
// 把既有数据重排（行主序逐格搬）。权威几何（声明/CSV）不走这里 —— 越界写忽略。
template <typename SlotList>
static void restrideInto(SlotList& cells, const QList<int>& fromLengths,
                         const QList<int>& toLengths, const QList<int>& toStrides,
                         qint64 fromCount, qint64 toCount)
{
    if (fromLengths.isEmpty() || fromLengths == toLengths || fromCount <= 0) return;
    if (cells.isEmpty()) return;
    SlotList moved(static_cast<qsizetype>(toCount));
    // 旧行主序逐格 -> 新偏移（最后一维连续，与取址公式同一口径）。
    const bool twoAxis = fromLengths.size() == 2;
    for (qint64 i = 0; i < fromCount && i < cells.size(); ++i) {
        qint64 dst = 0;
        if (fromLengths.size() <= 1) {
            dst = i;
        } else if (twoAxis) {
            const int d0 = fromLengths.at(0), d1 = fromLengths.at(1);
            const int x = int((i / d1) % d0), y = int(i % d1);
            dst = qint64(x) * toStrides.at(0) + qint64(y) * toStrides.at(1);
        } else {
            const int d1 = fromLengths.at(1), d2 = fromLengths.at(2);
            const int z = int(i % d2); const qint64 t = i / d2;
            const int y = int(t % d1), x = int(t / d1);
            dst = qint64(x) * toStrides.at(0) + qint64(y) * toStrides.at(1)
                  + qint64(z) * toStrides.at(2);
        }
        if (dst >= 0 && dst < toCount) moved[qsizetype(dst)] = cells.at(qsizetype(i));
    }
    cells = moved;
}

// 无几何（或运行期外接几何）的多维写：装/扩几何，返回新几何下的取址。
// 返回 -2 = 不能装（负下标），-1 = 无法取址（权威几何越界 / 下标个数不足）。
template <typename SlotList>
qint64 VariableStorage::extendExtents(QHash<QString, SlotGeometry>& map,
                                      const QString& storageKey, const QList<int>& indices,
                                      SlotList& cells)
{
    if (indices.size() < 2) return -1;
    for (int index : indices) if (index < 0) return -2;
    QList<int> lengths;
    for (int index : indices) lengths.append(index + 1);
    const auto it = map.constFind(storageKey);
    const bool known = it != map.constEnd();
    if (known && it.value().authoritative) return -1;   // 权威几何：越界写忽略
    if (known) {
        bool grew = false;
        for (int d = 0; d < lengths.size(); ++d) {
            const int was = it.value().lengths.value(d);
            if (was > lengths[d]) lengths[d] = was;     // 既有边界不缩
            if (lengths[d] != was || d >= it.value().lengths.size()) grew = true;
        }
        if (!grew) return addressOf(&it.value(), indices);
    }
    // 元素总数必须在**合并完既有边界之后**算：边界只扩不缩（count 是几何事实）。
    qint64 count = 1;
    for (int n : lengths) count *= n;
    QList<int> strides(lengths.size(), 1);
    qint64 acc = 1;
    for (int d = lengths.size() - 1; d >= 0; --d) { strides[d] = int(acc); acc *= lengths.at(d); }
    installGeometry(map, storageKey, lengths, strides, count, /*allocated=*/false,
                    /*authoritative=*/false);
    const SlotGeometry& after = *map.constFind(storageKey);
    if (known) {
        SlotGeometry before = it.value();
        restrideInto(cells, before.lengths, after.lengths, after.strides,
                     before.elementCount, after.elementCount);
    }
    return addressOf(&after, indices);
}

// ---- 唯一的写规则（读同理）：取址 -> 按需把容器长到 offset+1 -> 写 ----
bool VariableStorage::writeIntSlot(const QString& storageKey, const QList<int>& indices,
                                   qint64 value, const char* operation)
{
    ensureGeometry(storageKey);
    const auto it = m_geometries.constFind(storageKey);
    qint64 offset = it == m_geometries.constEnd() ? addressOf(nullptr, indices)
                                                  : addressOf(&it.value(), indices);
    if (offset < 0) {
        // 只有「没有几何」或「运行期外接几何」才允许扩边界；权威几何（声明/CSV）
        // 越界写忽略 —— 与旧实现「声明多维不长」同口径。
        if (indices.size() >= 2
            && (it == m_geometries.constEnd() || !it.value().authoritative)) {
            offset = extendExtents(m_geometries, storageKey, indices, m_intSlots[storageKey]);
        } else {
            qWarning().noquote() << "[var] 变量" << storageKey << operation
                                 << "越界（忽略）: indices=" << indices;
        }
    }
    if (offset < 0) return false;
    auto& cells = m_intSlots[storageKey];
    if (offset >= cells.size()) cells.resize(qsizetype(offset) + 1);  // 唯一的懒扩容
    cells[qsizetype(offset)] = value;
    return true;
}

bool VariableStorage::writeStrSlot(const QString& storageKey, const QList<int>& indices,
                                   const FlatStringValue& value, const char* operation)
{
    ensureGeometry(storageKey);
    const auto it = m_geometries.constFind(storageKey);
    qint64 offset = it == m_geometries.constEnd() ? addressOf(nullptr, indices)
                                                  : addressOf(&it.value(), indices);
    if (offset < 0) {
        // 只有「没有几何」或「运行期外接几何」才允许扩边界；权威几何（声明/CSV）
        // 越界写忽略 —— 与旧实现「声明多维不长」同口径。
        if (indices.size() >= 2
            && (it == m_geometries.constEnd() || !it.value().authoritative)) {
            offset = extendExtents(m_geometries, storageKey, indices, m_strSlots[storageKey]);
        } else {
            qWarning().noquote() << "[var] 变量" << storageKey << operation
                                 << "越界（忽略）: indices=" << indices;
        }
    }
    if (offset < 0) return false;
    auto& cells = m_strSlots[storageKey];
    if (offset >= cells.size()) cells.resize(qsizetype(offset) + 1);
    cells[qsizetype(offset)] = value;
    return true;
}

qint64 VariableStorage::readIntSlot(const QString& storageKey, const QList<int>& indices) const
{
    ensureGeometry(storageKey);
    const auto geometry = m_geometries.constFind(storageKey);
    const qint64 offset = geometry == m_geometries.constEnd()
        ? addressOf(nullptr, indices) : addressOf(&geometry.value(), indices);
    if (offset < 0) return 0;
    const auto it = m_intSlots.constFind(storageKey);
    if (it == m_intSlots.constEnd() || offset >= it.value().size()) return 0;
    return it.value().at(qsizetype(offset));
}

FlatStringValue VariableStorage::readStrSlot(const QString& storageKey,
                                             const QList<int>& indices) const
{
    ensureGeometry(storageKey);
    const auto geometry = m_geometries.constFind(storageKey);
    const qint64 offset = geometry == m_geometries.constEnd()
        ? addressOf(nullptr, indices) : addressOf(&geometry.value(), indices);
    if (offset < 0) return {};
    const auto it = m_strSlots.constFind(storageKey);
    if (it == m_strSlots.constEnd() || offset >= it.value().size()) return {};
    return it.value().at(qsizetype(offset));
}

// ---- CHARADATA：同一条扁平序列 + 角色 stride ----
// stride = 该变量每角色的元素数（几何 elementCount）；角色块基址 = 角色号·stride。
// 几何来自 shapeOf（声明 -> VariableSize.csv），首次访问时经 installGeometry 登记。
void VariableStorage::ensureCharaGeometry(const QString& storageKey)
{
    if (m_charaGeometries.contains(storageKey)) return;
    const ArrayShape shape = declaredShapeOfKey(storageKey);
    qint64 count = 1;
    QList<int> lengths = shape.lengths;
    QList<int> strides(lengths.size(), 1);
    if (!lengths.isEmpty()) {
        count = shape.elementCount > 0 ? shape.elementCount : 1;
        qint64 acc = 1;
        for (int d = lengths.size() - 1; d >= 0; --d) { strides[d] = int(acc); acc *= lengths.at(d); }
    }
    installGeometry(m_charaGeometries, storageKey, lengths, strides, count,
                    /*allocated=*/false, /*authoritative=*/!lengths.isEmpty());
}

const VariableStorage::SlotGeometry& VariableStorage::charaGeometry(const QString& storageKey) const
{
    static const SlotGeometry kScalar{};
    const auto it = m_charaGeometries.constFind(storageKey);
    return it == m_charaGeometries.constEnd() ? kScalar : it.value();
}

qint64 VariableStorage::readCharaIntSlot(const QString& storageKey, int charaSlot,
                                         const QList<int>& elements) const
{
    const SlotGeometry& geometry = charaGeometry(storageKey);
    const qint64 offset = addressOf(&geometry, elements);
    if (offset < 0) return 0;
    const qint64 slot = roleSlotOf(geometry, charaSlot, offset);
    const auto it = m_charaIntSlots.constFind(storageKey);
    if (it == m_charaIntSlots.constEnd() || slot < 0 || slot >= it.value().size()) return 0;
    return it.value().at(qsizetype(slot));
}

FlatStringValue VariableStorage::readCharaStrSlot(const QString& storageKey, int charaSlot,
                                                  const QList<int>& elements) const
{
    const SlotGeometry& geometry = charaGeometry(storageKey);
    const qint64 offset = addressOf(&geometry, elements);
    if (offset < 0) return {};
    const qint64 slot = roleSlotOf(geometry, charaSlot, offset);
    const auto it = m_charaStrSlots.constFind(storageKey);
    if (it == m_charaStrSlots.constEnd() || slot < 0 || slot >= it.value().size()) return {};
    return it.value().at(qsizetype(slot));
}

void VariableStorage::writeCharaIntSlot(const QString& storageKey, int charaSlot,
                                        const QList<int>& elements, qint64 value)
{
    ensureCharaGeometry(storageKey);
    qint64 offset = addressOf(&charaGeometry(storageKey), elements);
    if (offset < 0) {
        // 无几何/运行期外接几何才扩边界（每角色 stride 随 elementCount 一起变）。
        const auto it = m_charaGeometries.constFind(storageKey);
        if (elements.size() >= 2 && (it == m_charaGeometries.constEnd() || !it.value().authoritative))
            offset = extendExtents(m_charaGeometries, storageKey, elements, m_charaIntSlots[storageKey]);
        if (offset < 0) {
            qWarning().noquote() << "[var] 角色变量" << storageKey
                                 << "越界（忽略）: chara=" << charaSlot << "元素=" << elements;
            return;
        }
    }
    // 扩边界会重装几何（map.insert 可能使引用失效）：几何取扩后的事实。
    const qint64 slot = roleSlotOf(charaGeometry(storageKey), charaSlot, offset);
    auto& cells = m_charaIntSlots[storageKey];
    if (slot >= cells.size()) cells.resize(qsizetype(slot) + 1);   // 唯一的懒扩容
    cells[qsizetype(slot)] = value;
}

void VariableStorage::writeCharaStrSlot(const QString& storageKey, int charaSlot,
                                        const QList<int>& elements, const FlatStringValue& value)
{
    ensureCharaGeometry(storageKey);
    qint64 offset = addressOf(&charaGeometry(storageKey), elements);
    if (offset < 0) {
        // 无几何/运行期外接几何才扩边界（每角色 stride 随 elementCount 一起变）。
        const auto it = m_charaGeometries.constFind(storageKey);
        if (elements.size() >= 2 && (it == m_charaGeometries.constEnd() || !it.value().authoritative))
            offset = extendExtents(m_charaGeometries, storageKey, elements, m_charaStrSlots[storageKey]);
        if (offset < 0) {
            qWarning().noquote() << "[var] 角色变量" << storageKey
                                 << "越界（忽略）: chara=" << charaSlot << "元素=" << elements;
            return;
        }
    }
    // 扩边界会重装几何（map.insert 可能使引用失效）：几何取扩后的事实。
    const qint64 slot = roleSlotOf(charaGeometry(storageKey), charaSlot, offset);
    auto& cells = m_charaStrSlots[storageKey];
    if (slot >= cells.size()) cells.resize(qsizetype(slot) + 1);
    cells[qsizetype(slot)] = value;
}

// 几何缺失时从形状的唯一入口登记（声明 -> VariableSize.csv）。维数 1 不登记：
// 1D 的取址就是扁平序号，没有几何可言（见 addressOf(nullptr, ...)）。
void VariableStorage::ensureGeometry(const QString& storageKey) const
{
    if (m_geometries.contains(storageKey)) return;
    const ArrayShape shape = declaredShapeOfKey(storageKey);
    if (shape.dimension() < 2 || shape.elementCount <= 0) return;
    QList<int> strides(shape.lengths.size(), 1);
    qint64 acc = 1;
    for (int d = shape.lengths.size() - 1; d >= 0; --d) {
        strides[d] = int(acc);
        acc *= shape.lengths.at(d);
    }
    installGeometry(m_geometries, storageKey, shape.lengths, strides, shape.elementCount,
                    /*allocated=*/false, /*authoritative=*/true);
}

void VariableStorage::installMultiDimArray(const QString& storageKey, const QList<int>& lengths,
                                           const QList<int>& strides, bool stringArray,
                                           bool allocate)
{
    if (storageKey.isEmpty() || lengths.size() < 2) return;
    qint64 count = 1;
    for (int n : lengths) {
        if (n <= 0) return;                  // 维数未定稿：不注册
        if (count > std::numeric_limits<qint64>::max() / n) { count = -1; break; }
        count *= n;
    }
    if (count <= 0) return;
    const auto existing = m_geometries.constFind(storageKey);
    if (existing != m_geometries.constEnd()) {
        // 同键同形状：幂等（装载 pass 与按需分配都可能调用）。
        if (existing.value().lengths == lengths) return;
        qWarning().noquote() << "[var] 多维变量" << storageKey << "形状冲突，保留首次声明";
        return;
    }
    if (count > kMaxMultiDimElements) {
        // 不分配，但保留声明形状：越界判定/VARSIZE 仍与声明一致。
        qWarning().noquote() << "[var] 多维变量" << storageKey << "元素数" << count
                             << "超过上限" << kMaxMultiDimElements << "，拒绝分配";
        installGeometry(m_geometries, storageKey, lengths, strides, count,
                        /*allocated=*/false, /*authoritative=*/true);
        return;
    }
    installGeometry(m_geometries, storageKey, lengths, strides, count,
                    /*allocated=*/allocate, /*authoritative=*/true);
    if (!allocate) return;   // 只登记几何（CHARADATA：元素在角色容器里）
    if (stringArray)
        m_strSlots.insert(storageKey, QList<FlatStringValue>(qsizetype(count), FlatStringValue{}));
    else
        m_intSlots.insert(storageKey, QList<qint64>(qsizetype(count), 0));
}

qint64 VariableStorage::multiDimAllocatedElements() const
{
    qint64 n = 0;
    for (auto it = m_geometries.constBegin(); it != m_geometries.constEnd(); ++it) {
        if (!it.value().allocated) continue;
        n += it.value().elementCount;
    }
    return n;
}

qint64 VariableStorage::multiDimAllocatedBytes() const
{
    qint64 bytes = 0;
    for (auto it = m_geometries.constBegin(); it != m_geometries.constEnd(); ++it) {
        if (!it.value().allocated) continue;
        const bool isStr = m_strSlots.contains(it.key());
        bytes += it.value().elementCount * qint64(isStr ? sizeof(FlatStringValue) : sizeof(qint64));
    }
    return bytes;
}

QList<int> VariableStorage::multiDimLengths(const QString& storageKey) const
{
    const auto it = m_geometries.constFind(storageKey);
    return it == m_geometries.constEnd() ? QList<int>() : it.value().lengths;
}

void VariableStorage::clearForReload()
{
    VariableStorage defaults;
    std::swap(m_intSlots, defaults.m_intSlots);
    std::swap(m_strSlots, defaults.m_strSlots);
    std::swap(m_charaIntSlots, defaults.m_charaIntSlots);
    std::swap(m_charaStrSlots, defaults.m_charaStrSlots);
    std::swap(m_csvIntSlots, defaults.m_csvIntSlots);
    std::swap(m_csvStrSlots, defaults.m_csvStrSlots);
    std::swap(m_geometries, defaults.m_geometries);
    std::swap(m_charaGeometries, defaults.m_charaGeometries);
    std::swap(m_systemIntVars, defaults.m_systemIntVars);
    std::swap(m_systemStrVars, defaults.m_systemStrVars);
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

VariableStorage::ArrayShape VariableStorage::shapeOf(const QString& name,
                                                    eravar::ScopeId scope) const
{
        return shapeOfKey(storageName(name, scope));
}

VariableStorage::ArrayShape VariableStorage::declaredShapeOfKey(const QString& storageKey) const
{
        // 1) 声明（形状的所有者）。复合键 "段身份\x1fNAME" 拆回 (名字, 段身份文本)；
        //    裸键 = 广域声明。键已在 storageName 里做过 REF 别名解析，所以
        //    `#DIM REF` 形参问到的是**目标**数组的声明形状（对齐 C# ReferenceToken）。
        //    声明查不到时再按名字试 CHARADATA（角色数据是名字级身份，见
        //    VariableTable::findCharaData）。
        if (m_saveVariableTable) {
                // 「可用形状」= lengths 非空且至少一维 > 0。全 0 的是占位/未定稿
                //（`#DIMS REF X, 0, 0`、DYNAMIC），不算形状 —— 继续往下回落。
                const auto usable = [](const VariableDecl* d) {
                        if (d == nullptr || d->lengths.isEmpty()) return false;
                        for (int n : d->lengths)
                                if (n > 0) return true;
                        return false;
                };
                QString declScope;
                QString declName;
                eravar::splitPrivateStorageKey(storageKey, declScope, declName);
                const VariableDecl* d = m_saveVariableTable->find(declName, declScope);
                if (!usable(d)) d = m_saveVariableTable->findCharaData(declName);
                if (usable(d)) {
                        ArrayShape shape;
                        shape.lengths = d->lengths;
                        shape.elementCount = d->elementCount;
                        if (shape.elementCount <= 0) {
                                qint64 count = 1;
                                for (int n : shape.lengths) count *= n;
                                shape.elementCount = count;
                        }
                        return shape;
                }
        }
        // 2) 装载期尺寸表（系统变量 / CSV 名表 / 扩展变量）。这是**装载期**来源，
        //    不是「容器现在多大」。
        QString scratch;
        const QString& upper = eraUpperKey(storageKey, scratch);
        const auto product = [](const QList<int>& lengths) {
                qint64 count = 1;
                for (int n : lengths) count *= n;
                return count;
        };
        const QVector<int> sizes3 = m_variableConfig.getSize3D(upper);
        if (!sizes3.isEmpty()) {
                ArrayShape shape;
                for (int n : sizes3) shape.lengths.append(n);
                shape.elementCount = product(shape.lengths);
                return shape;
        }
        const QPair<int, int> sizes2 = m_variableConfig.getSize2D(upper);
        if (sizes2.first > 0 || sizes2.second > 0) {
                ArrayShape shape;
                shape.lengths = QList<int>{sizes2.first, sizes2.second};
                shape.elementCount = product(shape.lengths);
                return shape;
        }
        const int size1 = m_variableConfig.getSize1D(upper);
        if (size1 > 0) {
                ArrayShape shape;
                shape.lengths = QList<int>{size1};
                shape.elementCount = size1;
                return shape;
        }
        return {};
}

VariableStorage::ArrayShape VariableStorage::shapeOfKey(const QString& storageKey) const
{
        // 1)+2) 声明 -> VariableConfig（形状的所有者，只有一个函数）。
        if (const ArrayShape declared = declaredShapeOfKey(storageKey); !declared.isEmpty())
                return declared;
        // 3) 运行期外接几何：没有声明也没有装载期尺寸表 = 运行期才长出来的多维
        //    容器（REF 指向的未声明数组）。它的外接边界就是形状（元素只到 extents
        //    为止）；1D 容器的槽位数没有这个性质（任意前缀都有意义），所以 1D
        //    不进形状 —— 那是 allocatedSlots 的容器事实。
        const auto geometry = m_geometries.constFind(storageKey);
        if (geometry != m_geometries.constEnd() && !geometry.value().authoritative
            && geometry.value().lengths.size() >= 2) {
                ArrayShape shape;
                shape.lengths = geometry.value().lengths;
                shape.elementCount = geometry.value().elementCount;
                return shape;
        }
        return {};
}

int VariableStorage::arraySizeInCurrentFrame(const QString& name) const
{
        return arraySize(name, m_privateScopeId);   // 当前帧（旧语义）
}

int VariableStorage::arraySize(const QString& name, eravar::ScopeId scope) const
{
        const ArrayShape shape = shapeOf(name, scope);   // 形状的唯一入口
        if (!shape.isEmpty()) return shape.lengthOf(0);
        // 没有形状的名字（未声明的运行时 1D / 未定稿 DYNAMIC）：容器已分配槽位数。
        // 这是**容器事实**，不是形状 —— 形状唯一的答案在 shapeOf。
        return allocatedSlots(storageName(name, scope));
}

int VariableStorage::allocatedSlots(const QString& key) const
{
        // 容器当前已分配的槽位数（**不是形状**：装载期形状见 shapeOf）。
        // 旧名 arraySizeRaw 会被误当成形状读者（VARSIZE 曾回落到这里），故改名。
        // 多维：仍按声明首维报（形状已由 shapeOf 回答；这里只服务无形状的名字）。
        if (const auto geometry = m_geometries.constFind(key); geometry != m_geometries.constEnd())
            return geometry.value().lengths.value(0);

		// 局部/实参槽不在 m_global* 容器里，单独报尺寸
		if (key == QLatin1String("ITEMPRICE")) return m_itemprice.size();   // 常量容器（不在 m_global* 里）
		if (key == QLatin1String("LOCAL")) return m_localIntVars.size();
		if (key == QLatin1String("LOCALS")) return m_localStrVars.size();
		if (key == QLatin1String("ARG")) return m_argIntVars.size();
		if (key == QLatin1String("ARGS")) return m_argStrVars.size();
		if (auto it = m_strSlots.constFind(key); it != m_strSlots.constEnd()) return int(it->size());
		if (auto it = m_intSlots.constFind(key); it != m_intSlots.constEnd()) return int(it->size());
		return 0;
}

QList<int> VariableStorage::arrayLengthsInCurrentFrame(const QString& name) const
{
    return arrayLengths(name, m_privateScopeId);   // 当前帧（旧语义）
}

QList<int> VariableStorage::arrayLengths(const QString& name, eravar::ScopeId scope) const
{
    const ArrayShape shape = shapeOf(name, scope);   // 形状的唯一入口
    if (!shape.isEmpty()) return shape.lengths;
    // 无形状的名字退化为「已分配槽位数」这一个数（容器事实）。
    const int slotCount = allocatedSlots(storageName(name, scope));
    return slotCount > 0 ? QList<int>{slotCount} : QList<int>();
}

void VariableStorage::ensureArraySizeInCurrentFrame(const QString& name, int size, bool stringArray)
{
		ensureArraySize(name, size, stringArray, m_privateScopeId);
}

void VariableStorage::ensureArraySize(const QString& name, int size, bool stringArray,
                                      eravar::ScopeId scope)
{
		if (size <= 0) return;
		// 多维变量的容量由声明决定，不允许「第一次写时偷偷扩容」。
		const QString key = storageName(name, scope);
		if (isMultiDimArray(key)) return;
		// 唯一的懒扩容：把扁平序列长到 size（只增不减）。
		if (stringArray) {
			auto& values = m_strSlots[key];
			if (values.size() < size) values.resize(size);
		} else {
			auto& values = m_intSlots[key];
			if (values.size() < size) values.resize(size);
		}
}

// 私有初值直写（调用计划）：键已解析（FUNC\x1fNAME 复合键），
// 不再走 storageName 的作用域映射（复合键不在 m_references/m_privateNames 里，
// 解析恒为恒等 —— 与「作用域内裸名」两种写法结果一致）。
void VariableStorage::applyPrivateDefault(const QString& storageKey, bool isString, int ensureSize,
                                          const QList<qint64>& defaultInt, const QStringList& defaultStr)
{
		// 初值按**扁平序号**写（行主序，最后一维连续），与 #DIM X, 2,3 = a,b,c,d
		// 的 C# 口径一致；多维的容量已在装载期按声明分配。
		if (isString) {
			auto& values = m_strSlots[storageKey];
			if (ensureSize > 0 && values.size() < ensureSize) values.resize(ensureSize);
			for (int i = 0; i < defaultStr.size(); ++i) {
				if (values.size() <= i) values.resize(i + 1);
				values[i] = FlatStringValue(defaultStr.at(i));
			}
		} else {
			auto& values = m_intSlots[storageKey];
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
void VariableStorage::setGlobalInt3DInCurrentFrame(const QString &name, int x, int y, int z, qint64 val)
{
		setGlobalInt3DRaw(storageNameInCurrentFrame(name), x, y, z, val);
}

qint64 VariableStorage::getGlobalInt3DInCurrentFrame(const QString &name, int x, int y, int z) const
{
		return getGlobalInt3DRaw(storageNameInCurrentFrame(name), x, y, z);
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

VariableStorage::IntLocus VariableStorage::resolveIntLocus(const QString &name,
                                                           const QList<int> &indices,
                                                           eravar::ScopeId scope) const
{
		// 行序即语义：这是「名字 + 下标 -> 整数存储」判定的**唯一定义点**。
		// readIntByName / writeIntByName 只按 IntLocusKind 施加；新增/调整优先级
		// 必须改这里，并由 test_name_locus 的护栏钉住。
		IntLocus l;
		l.e0 = indices.value(0);
		// 1) 参数值绑定：按名命中，不看 scope，也不构造存储键。
		if (hasParameter(name)) {
				l.kind = IntLocusKind::Parameter;
				return l;
		}
		// 名字 -> 存储键只解析一次（scope 是显式参数：私有名按该函数的名表解析）。
		l.key = storageName(name, scope);
		// 2) 角色数据变量：按 (角色号, 元素下标) 定位；字符串槽整型读恒 0 / 写忽略。
		if (isCharaDataVariable(name)) {
				if (isCharaDataString(name)) {
						l.kind = IntLocusKind::CharaStrAsInt;
						return l;
				}
				QList<int> elems;
				reduceCharaArgs(name, indices, l.charaId, elems);
				l.e0 = elems.value(0);
				l.e1 = elems.value(1);
				l.kind = charaDataDimension(name) >= 2 ? IntLocusKind::CharaInt2D
				                                       : IntLocusKind::CharaInt1D;
				return l;
		}
		const QString upper = eraUpperKey(name);
		// 3) / 4) ARG / LOCAL。
		if (upper == QLatin1String("ARG")) {
				l.kind = IntLocusKind::Arg;
				return l;
		}
		if (upper == QLatin1String("LOCAL")) {
				l.kind = IntLocusKind::Local;
				return l;
		}
		// 5) ARGS / LOCALS / RESULTS：字符串槽。整型写忽略；整型读落到 1D 槽
		//    （今天的不对称，readIntByName 的 StringSlot 分支就是 getGlobalInt1DRaw）。
		if (upper == QLatin1String("ARGS") || upper == QLatin1String("LOCALS")
		    || upper == QLatin1String("RESULTS")) {
				l.kind = IntLocusKind::StringSlot;
				return l;
		}
		// 6) 系统变量（FLAG/DA/TA/...）。
		if (hasSystemVariable(name)) {
				l.kind = IntLocusKind::System;
				return l;
		}
		// 7) / 8) 声明维数 >= 2 且给了 >= 2 个下标 -> 2D/3D；否则全局 1D
		//         （旧 writeLhs 的同款判定：1D 声明给多下标时只取第 0 个）。
		const int dim = intDimension(name);
		if (indices.size() >= 2 && dim >= 2) {
				if (indices.size() >= 3 && dim >= 3) {
						l.e0 = indices.at(0);
						l.e1 = indices.at(1);
						l.e2 = indices.at(2);
						l.kind = IntLocusKind::Global3D;
				} else {
						l.e0 = indices.at(0);
						l.e1 = indices.at(1);
						l.kind = IntLocusKind::Global2D;
				}
				return l;
		}
		l.kind = IntLocusKind::Global1D;
		return l;
}

VariableStorage::StrLocus VariableStorage::resolveStrLocus(const QString &nameOrKey,
                                                            const QList<int> &indices,
                                                            eravar::ScopeId scope,
                                                            StrKeyState keyState) const
{
		// 行序即语义：这是「名字 -> 字符串存储」判定的**唯一定义点**。
		// getStringSlot / getStringSlotRaw / setStringSlot 只按 StrLocusKind 施加；
		// 新增/调整优先级必须改这里，并由 test_string_locus 的护栏钉住。
		StrLocus l;
		l.x = indices.value(0);
		const int y = indices.value(1), z = indices.value(2);
		// 1) 名字 -> 存储键只解析一次。Raw 入口（StrKeyState::ResolvedKey）的键
		//    是装载期算好的静态键，**绝不再套一次 storageName**（否则就是按 scope
		//    重新解析，私有名/REF 别名都会把键改道）。
		l.key = keyState == StrKeyState::Name ? storageName(nameOrKey, scope) : nameOrKey;
		// 2) 负下标：读写都不发生。放在键解析之后与旧实现同序（解析无副作用）。
		if (l.x < 0 || y < 0 || z < 0) {
				l.kind = StrLocusKind::NegativeIndex;
				return l;
		}
		// 3) LOCALS / 4) ARGS：函数局部字符串槽（读越界给空，写懒扩容）。
		if (l.key == QLatin1String("LOCALS")) {
				l.kind = StrLocusKind::LocalStr;
				return l;
		}
		if (l.key == QLatin1String("ARGS")) {
				l.kind = StrLocusKind::ArgStr;
				return l;
		}
		// 5) SAVEDATA_TEXT：系统字符串，单槽（下标不参与取址）。
		if (l.key == QLatin1String("SAVEDATA_TEXT")) {
				l.kind = StrLocusKind::SavedataText;
				return l;
		}
		// 6) 角色字符串变量（CSTR / NAME / 用户 #DIMS CHARADATA）。
		//    **两种入参都命中这一行**：按名入口传名字；键语义入口（ResolvedKey）
		//    传的键可能**就是**角色变量名本身 —— REF 别名把形参键解析成目标键
		//    后，目标可以是一个角色字符串变量（`#DIMS REF PS` ← `CSTR`），此时
		//    键文本与名字文本相同。只认名字会让这类读掉进 7) 的扁平槽（恒空）。
		//    私有键/复合键不是角色变量名（isCharaDataString 按整串查注册表），
		//    仍落到 7) —— 与旧的 Raw 语义只在这里多认出「键即角色名」一种。
		{
				const QString& charaName = keyState == StrKeyState::Name ? nameOrKey : l.key;
				if (isCharaDataString(charaName)) {
						int character = 0;
						reduceCharaArgs(charaName, indices, character, l.elements);
						l.charaSlot = resolveCharaIndex(character);
						l.kind = StrLocusKind::CharaStr;
						return l;
				}
		}
		// 7) 普通扁平槽：1D 与多维共用一条序列（取址在 readStrSlot/writeStrSlot）。
		l.kind = StrLocusKind::GlobalStr;
		return l;
}

FlatStringValue VariableStorage::readStrLocus(const StrLocus &locus,
                                              const QList<int> &indices) const
{
		// 穷尽所有 kind（无 default）：漏一个 case 在 -Wswitch + -Werror 下编译失败。
		switch (locus.kind) {
		case StrLocusKind::NegativeIndex: return {};
		case StrLocusKind::LocalStr:      return m_localStrVars.value(locus.x);
		case StrLocusKind::ArgStr:        return m_argStrVars.value(locus.x);
		case StrLocusKind::SavedataText:  return m_systemStrVars.value(locus.key);
		case StrLocusKind::CharaStr:
				if (locus.charaSlot < 0) return {};
				return readCharaStrSlot(locus.key, locus.charaSlot, locus.elements);
		case StrLocusKind::GlobalStr:     return readStrSlot(locus.key, indices);
		}
		return {};   // 编译器兜底：switch 已穷尽所有枚举值
}

qint64 VariableStorage::readIntByName(const QString &name, const QList<int> &indices,
                                     eravar::ScopeId scope) const
{
		const IntLocus l = resolveIntLocus(name, indices, scope);
		// 穷尽所有 kind（无 default）：漏一个 case 在 -Wswitch + -Werror 下编译失败。
		switch (l.kind) {
		case IntLocusKind::Parameter:   return parameter(name).toLongLong();
		case IntLocusKind::CharaInt1D:  return getCharaInt(l.key, l.charaId, l.e0);
		case IntLocusKind::CharaInt2D:  return getCharaInt3D(l.key, l.charaId, l.e0, l.e1);
		case IntLocusKind::CharaStrAsInt: return 0;
		case IntLocusKind::Arg:         return getArgInt(l.e0);
		case IntLocusKind::Local:       return getLocalInt(l.e0);
		case IntLocusKind::StringSlot:  return getGlobalInt1DRaw(l.key, l.e0);
		case IntLocusKind::System:      return getSystemVariable(name, l.e0);
		case IntLocusKind::Global3D:    return getGlobalInt3DRaw(l.key, l.e0, l.e1, l.e2);
		case IntLocusKind::Global2D:    return getGlobalInt2DRaw(l.key, l.e0, l.e1);
		case IntLocusKind::Global1D:    return getGlobalInt1DRaw(l.key, l.e0);
		}
		return 0;   // 编译器兜底：switch 已穷尽所有枚举值
}

void VariableStorage::writeIntByName(const QString &name, const QList<int> &indices, qint64 value,
                                     eravar::ScopeId scope)
{
		const IntLocus l = resolveIntLocus(name, indices, scope);
		// 穷尽所有 kind（无 default）：漏一个 case 在 -Wswitch + -Werror 下编译失败。
		switch (l.kind) {
		case IntLocusKind::Parameter:   setParameter(name, value); return;
		case IntLocusKind::CharaInt1D:  setCharaInt(l.key, l.charaId, l.e0, value); return;
		case IntLocusKind::CharaInt2D:  setCharaInt3D(l.key, l.charaId, l.e0, l.e1, value); return;
		case IntLocusKind::CharaStrAsInt: return;   // 字符串：走 assignStringVariable
		case IntLocusKind::Arg:         setArgInt(l.e0, value); return;
		case IntLocusKind::Local:       setLocalInt(l.e0, value); return;
		case IntLocusKind::StringSlot:  return;   // 字符串槽：整型写忽略（不对称）
		case IntLocusKind::System:      setSystemVariable(name, l.e0, value); return;
		case IntLocusKind::Global3D:    setGlobalInt3DRaw(l.key, l.e0, l.e1, l.e2, value); return;
		case IntLocusKind::Global2D:    setGlobalInt2DRaw(l.key, l.e0, l.e1, value); return;
		case IntLocusKind::Global1D:    setGlobalInt1DRaw(l.key, l.e0, value); return;
		}
}

// ================= 角色整型变量 =================
// 扁平 + 角色 stride：槽位 = 角色号·elementCount + 元素偏移。Raw：按**运行时
// 下标**直接存取（不做 运行时->模板 翻译），供角色列表自身使用。
void VariableStorage::setCharaIntRaw(const QString &name, int charaId, int index, qint64 value)
{
		if (charaId < 0 || index < 0) return;
		writeCharaIntSlot(storageName(name, eravar::kGlobalScope), charaId, {index}, value);
}

void VariableStorage::setCharaInt(const QString &name, int charaId, int index, qint64 value)
{
		if (charaId < 0 || index < 0) return;
		setCharaIntRaw(name, resolveCharaIndex(charaId), index, value);
}

namespace {
// 角色变量「全部角色全部元素归零」：扁平序列整体 fill（与旧实现逐角色填充同义）。
void zeroCharaSlots(QHash<QString, QList<qint64>>& cells, const QString& key)
{
	auto it = cells.find(key);
	if (it == cells.end()) return;
	it.value().fill(0);
}
} // namespace

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
		for (const char* k : kCharaKeys)
				zeroCharaSlots(m_charaIntSlots, storageName(QString::fromLatin1(k), eravar::kGlobalScope));
}

void VariableStorage::updateAfterInputCom()
{
		// 各角色 NOWEX = 0（对齐 C#：选择中以外的角色也全部重置）
		zeroCharaSlots(m_charaIntSlots, storageName(QStringLiteral("NOWEX"), eravar::kGlobalScope));
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
				setGlobalStr1D(QStringLiteral("TSTR"), i, QString(), eravar::kGlobalScope);
		}

		// 全角色数值归零（C#：GOTJUEL/TEQUIP/EX/STAIN/PALAM/SOURCE/TCVAR）。
		static const char* const kCharaKeys[] = {
			"GOTJUEL", "TEQUIP", "EX", "PALAM", "SOURCE", "TCVAR", "STAIN",
		};
		for (const char* k : kCharaKeys)
				zeroCharaSlots(m_charaIntSlots, storageName(QString::fromLatin1(k), eravar::kGlobalScope));
		// STAIN 走 C# 的 setDefaultStain()：_REPLACE.CSV 初值未装载时按 0
		// （与 RESET_STAIN 内建指令取同一语义）。
}

qint64 VariableStorage::getCharaInt(const QString &name, int charaId, int index) const
{
		if (charaId < 0 || index < 0) return 0;
		charaId = resolveCharaIndex(charaId);
		if (charaId < 0) return 0;   // VOID 角色（ADDVOIDCHARA）：无模板数据
		return readCharaIntSlot(storageName(name, eravar::kGlobalScope), charaId, {index});
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
		writeCharaStrSlot(storageName(name, eravar::kGlobalScope), charaId, {index}, FlatStringValue(value));
}

QString VariableStorage::getCharaStr(const QString &name, int charaId, int index) const
{
		if (charaId < 0 || index < 0) return QString();
		charaId = resolveCharaIndex(charaId);
		if (charaId < 0) return QString();   // VOID 角色：无模板数据
		return readCharaStrSlot(storageName(name, eravar::kGlobalScope), charaId, {index}).text;
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
		purge(m_intSlots);
		purge(m_strSlots);
		// 外接几何随容器一起作废（没有容器的形状不是形状）。
		for (auto it = m_geometries.begin(); it != m_geometries.end(); ) {
			if (it.value().authoritative) ++it;
			else it = m_geometries.erase(it);
		}
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
		// key 已经是存储键（私有名 = FUNC\x1fNAME 复合键）：不再按当前帧解析。
		const QString storageKey = resolvedStorageName(key, eravar::kGlobalScope);
		const QList<int> dims = d.lengths.isEmpty() ? QList<int>{1} : d.lengths;
		const int count = erasave::shapeCount(dims);
		const int defaultCount = d.type == OperandType::Str ? int(d.defaultStr.size())
		                                                    : int(d.defaultInt.size());
		// 声明过的变量（1D 与多维同一容器）：整条扁平序列 = dims 的乘积。
		if (d.type == OperandType::Str) {
			auto& vec = m_strSlots[storageKey];
			if (vec.size() < count) vec.resize(count);
			std::fill_n(vec.begin(), count, FlatStringValue{});
			const int n = qMin(count, defaultCount);
			for (int i = 0; i < n; ++i) vec[i] = FlatStringValue(d.defaultStr.at(i));
		} else {
			auto& vec = m_intSlots[storageKey];
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
		const auto keptInt = keep(m_intSlots);
		const auto keptStr = keep(m_strSlots);
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
		purgePrivate(m_intSlots);
		purgePrivate(m_strSlots);
		for (auto it = m_geometries.begin(); it != m_geometries.end(); ) {
			if (it.value().authoritative) ++it;
			else it = m_geometries.erase(it);   // 外接几何随容器作废
		}
		// 私有名表是**装载期元数据**（脚本声明的一部分），不是每局数据：
		// RESETDATA 不能清掉它 —— 安装后 setPrivateScope 已不再建表，清掉就会让
		// 所有私有名静默退化成裸名（又是另一个变量）。未安装（测试手工路径）时
		// 保持旧行为清空。
		if (!m_privateTableInstalled) m_privateNames.clear();
		m_privateScopeId = eravar::kGlobalScope;

		restoreDeclaredDefaults();
		// 角色数据回默认值。本移植把 CSV 模板默认值**直接写进角色存储**
		// （m_charaIntSlots/m_charaStrSlots，按模板号·stride 存放），ADDCHARA/LOADCHARA 之后
		// 都从这些容器取 NAME/ABL… 模板默认值 —— 因此这里必须从模板快照恢复，
		// 而不是清空；否则清空后新增/载入的角色 NAME/ABL… 全空（组 9 LOADCHARA
		// NAME、组 28 ABL:技巧 的回归根因）。运行时 2D 角色数组（无 CSV 快照）仍清空。
		m_charaIntSlots = m_csvIntSlots;
		m_charaStrSlots = m_csvStrSlots;
		// 运行期才长出来的角色几何（无声明）随运行时数据一起作废；
		// 声明/CSV 定形的几何保留（快照仍按同一几何解释）。
		for (auto it = m_charaGeometries.begin(); it != m_charaGeometries.end(); ) {
			if (it.value().authoritative) ++it;
			else it = m_charaGeometries.erase(it);
		}
		// 本地槽（LOCAL/ARG/LOCALS/ARGS）与形参
		m_localIntVars.fill(0);
		m_argIntVars.fill(0);
		m_localStrVars.fill(FlatStringValue{});
		m_argStrVars.fill(FlatStringValue{});
		m_parameters.clear();
		const auto restore = [](auto& map, const auto& saved) {
			for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) map.insert(it.key(), it.value());
		};
		restore(m_intSlots, keptInt);
		restore(m_strSlots, keptStr);
		qDebug() << "[var] RESETDATA 变量回默认值，角色列表已清空";
}

// ================= CSV 模板快照 =================
// 装载角色 CSV 之后调用一次：把当前角色存储整体复制为「模板」。
// 此后 CSV* 函数读快照，脚本对 VAR:角色:下标 的修改不再影响模板值。
void VariableStorage::snapshotCharaTemplates()
{
		// 与运行时同布局的扁平快照（按模板号而不是运行时下标）。
		m_csvIntSlots = m_charaIntSlots;
		m_csvStrSlots = m_charaStrSlots;
		qDebug() << "[chara] CSV 模板快照：整型变量" << m_csvIntSlots.size()
				 << "字符串" << m_csvStrSlots.size();
}

qint64 VariableStorage::csvCharaInt(const QString& name, int charaId, int index) const
{
		if (charaId < 0 || index < 0) return 0;
		const QString storageKey = storageName(name, eravar::kGlobalScope);
		const SlotGeometry& geometry = charaGeometry(storageKey);
		const qint64 stride = geometry.elementCount > 0 ? geometry.elementCount : 1;
		const qint64 slot = qint64(charaId) * stride + index;
		const auto it = m_csvIntSlots.constFind(storageKey);
		if (it == m_csvIntSlots.constEnd() || slot >= it.value().size()) return 0;
		return it.value().at(qsizetype(slot));
}

QString VariableStorage::csvCharaStr(const QString& name, int charaId, int index) const
{
		if (charaId < 0 || index < 0) return QString();
		const QString storageKey = storageName(name, eravar::kGlobalScope);
		const SlotGeometry& geometry = charaGeometry(storageKey);
		const qint64 stride = geometry.elementCount > 0 ? geometry.elementCount : 1;
		const qint64 slot = qint64(charaId) * stride + index;
		const auto it = m_csvStrSlots.constFind(storageKey);
		if (it == m_csvStrSlots.constEnd() || slot >= it.value().size()) return QString();
		return it.value().at(qsizetype(slot)).text;
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

// ================= 全局整型 / 字符串 1D/2D/3D =================
// 三种维数、名字访问与 Raw 访问都是同一对原语（readIntSlot / writeIntSlot、
// readStrSlot / writeStrSlot）的薄包装：维数只体现在 indices 的个数上。
void VariableStorage::setGlobalInt1DInCurrentFrame(const QString &name, int x, qint64 val)
{
		if (x < 0) return;
		setGlobalInt1DRaw(storageNameInCurrentFrame(name), x, val);
}

void VariableStorage::setGlobalStr1DInCurrentFrame(const QString &name, int x, const QString &value)
{
		if (x < 0) return;
		writeStrSlot(storageNameInCurrentFrame(name), {x}, FlatStringValue(value), "写");
}

QString VariableStorage::getGlobalStr1DInCurrentFrame(const QString &name, int x) const
{
        return getGlobalStr1DRaw(storageNameInCurrentFrame(name), x);
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
        return readStrSlot(storageKey, {x}).text;
}

void VariableStorage::setGlobalStr2DInCurrentFrame(const QString &name, int x, int y, const QString &value)
{
		if (x < 0 || y < 0) return;
		writeStrSlot(storageNameInCurrentFrame(name), {x, y}, FlatStringValue(value), "写");
}

QString VariableStorage::getGlobalStr2DInCurrentFrame(const QString &name, int x, int y) const
{
		if (x < 0 || y < 0) return QString();
		return readStrSlot(storageNameInCurrentFrame(name), {x, y}).text;
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

qint64 VariableStorage::getGlobalInt1DInCurrentFrame(const QString &name, int x) const
{
		return getGlobalInt1DRaw(storageNameInCurrentFrame(name), x);
}

// ================= Raw 访问族（键已解析；见 variable_storage.h 说明） =================
qint64 VariableStorage::getGlobalInt1DRaw(const QString& storageKey, int x) const
{
		if (x < 0) return 0;
		// 多维变量的「扁平写法」：与 X:i:j:k 共用同一条序列（脚本层由静态检查
		// 要求给足下标；这里是内部/旧路径的统一口径）。
		return readIntSlot(storageKey, {x});
}

qint64 VariableStorage::getGlobalInt2DRaw(const QString& storageKey, int x, int y) const
{
		if (x < 0 || y < 0) return 0;
		return readIntSlot(storageKey, {x, y});
}

qint64 VariableStorage::getGlobalInt3DRaw(const QString& storageKey, int x, int y, int z) const
{
		if (x < 0 || y < 0 || z < 0) return 0;
		return readIntSlot(storageKey, {x, y, z});
}

QString VariableStorage::getGlobalStr2DRaw(const QString& storageKey, int x, int y) const
{
		if (x < 0 || y < 0) return QString();
		return readStrSlot(storageKey, {x, y}).text;
}

QString VariableStorage::getGlobalStr3DRaw(const QString& storageKey, int x, int y, int z) const
{
		if (x < 0 || y < 0 || z < 0) return QString();
		return readStrSlot(storageKey, {x, y, z}).text;
}

void VariableStorage::setGlobalInt1DRaw(const QString& storageKey, int x, qint64 value)
{
		if (x < 0) return;
		writeIntSlot(storageKey, {x}, value, "写");
}

void VariableStorage::setGlobalInt2DRaw(const QString& storageKey, int x, int y, qint64 value)
{
		if (x < 0 || y < 0) return;
		writeIntSlot(storageKey, {x, y}, value, "写");
}

void VariableStorage::setGlobalInt3DRaw(const QString& storageKey, int x, int y, int z, qint64 value)
{
		if (x < 0 || y < 0 || z < 0) return;
		writeIntSlot(storageKey, {x, y, z}, value, "写");
}

// ================= 显式作用域的按名访问 =================
// 生产代码只走这一组：scope 是**必填**参数（没有默认值），因此「用哪个段解析
// 这个名字」在每一行调用上都可读。语义与 *InCurrentFrame 完全一致 —— 后者
// 只是把 scope 写死成 m_privateScopeId 的旧形状，留给测试钉语义。
void VariableStorage::setGlobalInt1D(const QString& name, int x, qint64 val, eravar::ScopeId scope)
{
		if (x < 0) return;
		setGlobalInt1DRaw(storageName(name, scope), x, val);
}

qint64 VariableStorage::getGlobalInt1D(const QString& name, int x, eravar::ScopeId scope) const
{
		return getGlobalInt1DRaw(storageName(name, scope), x);
}

void VariableStorage::setGlobalInt2D(const QString& name, int x, int y, qint64 val,
                                     eravar::ScopeId scope)
{
		if (x < 0 || y < 0) return;
		setGlobalInt2DRaw(storageName(name, scope), x, y, val);
}

qint64 VariableStorage::getGlobalInt2D(const QString& name, int x, int y,
                                       eravar::ScopeId scope) const
{
		if (x < 0 || y < 0) return 0;
		return getGlobalInt2DRaw(storageName(name, scope), x, y);
}

void VariableStorage::setGlobalInt3D(const QString& name, int x, int y, int z, qint64 val,
                                     eravar::ScopeId scope)
{
		if (x < 0 || y < 0 || z < 0) return;
		setGlobalInt3DRaw(storageName(name, scope), x, y, z, val);
}

qint64 VariableStorage::getGlobalInt3D(const QString& name, int x, int y, int z,
                                       eravar::ScopeId scope) const
{
		if (x < 0 || y < 0 || z < 0) return 0;
		return getGlobalInt3DRaw(storageName(name, scope), x, y, z);
}

void VariableStorage::setGlobalStr1D(const QString& name, int x, const QString& value,
                                     eravar::ScopeId scope)
{
		if (x < 0) return;
		writeStrSlot(storageName(name, scope), {x}, FlatStringValue(value), "写");
}

QString VariableStorage::getGlobalStr1D(const QString& name, int x, eravar::ScopeId scope) const
{
		return getGlobalStr1DRaw(storageName(name, scope), x);
}

void VariableStorage::setGlobalStr2D(const QString& name, int x, int y, const QString& value,
                                     eravar::ScopeId scope)
{
		if (x < 0 || y < 0) return;
		writeStrSlot(storageName(name, scope), {x, y}, FlatStringValue(value), "写");
}

QString VariableStorage::getGlobalStr2D(const QString& name, int x, int y,
                                        eravar::ScopeId scope) const
{
		if (x < 0 || y < 0) return QString();
		return readStrSlot(storageName(name, scope), {x, y}).text;
}

void VariableStorage::setGlobalInt2DInCurrentFrame(const QString &name, int x, int y, qint64 val)
{
		setGlobalInt2DRaw(storageNameInCurrentFrame(name), x, y, val);
}

qint64 VariableStorage::getGlobalInt2DInCurrentFrame(const QString &name, int x, int y) const
{
		if (x < 0 || y < 0) return 0;
		return getGlobalInt2DRaw(storageNameInCurrentFrame(name), x, y);
}

// ================= 角色整型 3D 实现 =================
void VariableStorage::setCharaInt3D(const QString &name, int charaId, int x, int y, qint64 value)
{
		if (charaId < 0 || x < 0 || y < 0) return;
		charaId = resolveCharaIndex(charaId);
		if (charaId < 0) return;   // VOID 角色：无模板数据
		writeCharaIntSlot(storageName(name, eravar::kGlobalScope), charaId, {x, y}, value);
}

qint64 VariableStorage::getCharaInt3D(const QString &name, int charaId, int x, int y) const
{
		if (charaId < 0 || x < 0 || y < 0) return 0;
		charaId = resolveCharaIndex(charaId);
		if (charaId < 0) return 0;   // VOID 角色：无模板数据
		return readCharaIntSlot(storageName(name, eravar::kGlobalScope), charaId, {x, y});
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
		// 漏在这里会让运行期把它当成用户全局变量（退回 m_intSlots）——
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
    case erasave::Slot::GlobalInt1D:
        appendInts(v, [&](int x, int, int) { return readIntSlot(key, {x}); });
        break;
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
    case erasave::Slot::CharaInt1D:
        appendInts(v, [&](int x, int, int) {
            return charaSlot >= 0 ? readCharaIntSlot(key, charaSlot, {x}) : 0; });
        break;
    case erasave::Slot::CharaInt2D:
        appendInts(v, [&](int x, int y, int) {
            return charaSlot >= 0 ? readCharaIntSlot(key, charaSlot, {x, y}) : 0; });
        break;
    case erasave::Slot::CharaStr1D:
        appendStrings(v, [&](int x, int, int) {
            return charaSlot >= 0 ? readCharaStrSlot(key, charaSlot, {x}).text : QString(); });
        break;
    case erasave::Slot::CharaStr2D:
        appendStrings(v, [&](int x, int y, int) {
            return charaSlot >= 0 ? readCharaStrSlot(key, charaSlot, {x, y}).text : QString(); });
        break;
    }
    return v;
}

void VariableStorage::writeSaveEntry(const erasave::Entry& entry, int charaSlot, int x, int y, int z,
                                     qint64 number, const QString& text)
{
    const QString& key = storageKeyFor(entry.key);
    switch (entry.slot) {
    case erasave::Slot::SystemInt1D: setSystemVariable(entry.name, x, number); break;
    case erasave::Slot::GlobalInt1D: writeIntSlot(key, {x}, number, "读档写"); break;
    case erasave::Slot::GlobalStr1D: writeStrSlot(key, {x}, FlatStringValue(text), "读档写"); break;
    case erasave::Slot::GlobalInt2D: writeIntSlot(key, {x, y}, number, "读档写"); break;
    case erasave::Slot::GlobalStr2D: writeStrSlot(key, {x, y}, FlatStringValue(text), "读档写"); break;
    case erasave::Slot::GlobalInt3D: writeIntSlot(key, {x, y, z}, number, "读档写"); break;
    case erasave::Slot::GlobalStr3D: writeStrSlot(key, {x, y, z}, FlatStringValue(text), "读档写"); break;
    case erasave::Slot::CharaInt1D:
        if (charaSlot >= 0) writeCharaIntSlot(key, charaSlot, {x}, number);
        break;
    case erasave::Slot::CharaStr1D:
        if (charaSlot >= 0) writeCharaStrSlot(key, charaSlot, {x}, FlatStringValue(text));
        break;
    case erasave::Slot::CharaInt2D:
        if (charaSlot >= 0) writeCharaIntSlot(key, charaSlot, {x, y}, number);
        break;
    case erasave::Slot::CharaStr2D:
        if (charaSlot >= 0) writeCharaStrSlot(key, charaSlot, {x, y}, FlatStringValue(text));
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
    const auto keptInt=preserve(m_intSlots); const auto keptStr=preserve(m_strSlots);
    if(normal) {
        resetForNewGame(); setLocalContext({});
        // 声明初值恢复 = resetForNewGame 的同一件事，走同一个批量实现
        //（旧代码在这里还有一份逐元素 setGlobal*D + coordinates() 的手写副本，
        // 400 万个元素读一次档付一次）。
        restoreDeclaredDefaults();
        applyBinarySave(globals);
        const auto restore=[](auto& map,const auto& kept) { for(auto it=kept.begin();it!=kept.end();++it) map.insert(it.key(),it.value()); };
        restore(m_intSlots,keptInt); restore(m_strSlots,keptStr);
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

FlatStringValue VariableStorage::getStringSlotInCurrentFrame(const QString& name, const QList<int>& indices) const {
    return getStringSlot(name, indices, m_privateScopeId);
}

FlatStringValue VariableStorage::getStringSlot(const QString& name, const QList<int>& indices,
                                               eravar::ScopeId scope) const {
    // 解析一次（resolveStrLocus），读侧只有一份施加点（readStrLocus）。
    const StrLocus locus = resolveStrLocus(name, indices, scope, StrKeyState::Name);
    return readStrLocus(locus, indices);
}

// 键已解析的 getStringSlot（「变量分词」快路径用）：跳过 storageName，其余分派
// 与 getStringSlot 一致。**必须返回 FlatStringValue（带 markup 分片）** —— HTML
// 累积（html += HTML_BUTTON(...)）与打印模板都靠 markup，不能只回裸文本。
// 角色/系统字符串名（CSTR/SAVEDATA_TEXT…）在分词期已判 Dynamic，不进这里。
FlatStringValue VariableStorage::getStringSlotRaw(const QString& storageKey, const QList<int>& indices) const {
    // 键已解析：同一个判定链，但第 1 步跳过 storageName（不按 scope 重新解析）。
    const StrLocus locus = resolveStrLocus(storageKey, indices, eravar::kGlobalScope,
                                           StrKeyState::ResolvedKey);
    return readStrLocus(locus, indices);
}

bool VariableStorage::setStringSlotInCurrentFrame(const QString& name, const QList<int>& indices, const FlatStringValue& value) {
    return setStringSlot(name, indices, value, m_privateScopeId);
}

bool VariableStorage::setStringSlot(const QString& name, const QList<int>& indices,
                                    const FlatStringValue& value, eravar::ScopeId scope) {
    // 解析一次（与读侧同一个 resolveStrLocus），写侧只有这一份施加点。
    // 尾部动作的差异保留在这里：LOCALS/ARGS 懒扩容，其余按各自存储写。
    const StrLocus locus = resolveStrLocus(name, indices, scope, StrKeyState::Name);
    // 穷尽所有 kind（无 default）：漏一个 case 在 -Wswitch + -Werror 下编译失败。
    switch (locus.kind) {
    case StrLocusKind::NegativeIndex: return false;
    case StrLocusKind::LocalStr:
        if (m_localStrVars.size() <= locus.x) m_localStrVars.resize(locus.x + 1);
        m_localStrVars[locus.x] = value;
        return true;
    case StrLocusKind::ArgStr:
        if (m_argStrVars.size() <= locus.x) m_argStrVars.resize(locus.x + 1);
        m_argStrVars[locus.x] = value;
        return true;
    case StrLocusKind::SavedataText:
        m_systemStrVars[locus.key] = value;
        return true;
    case StrLocusKind::CharaStr:
        if (locus.charaSlot < 0) return false;
        writeCharaStrSlot(locus.key, locus.charaSlot, locus.elements, value);
        return true;
    case StrLocusKind::GlobalStr:
        writeStrSlot(locus.key, indices, value, "写");
        return true;
    }
    return false;   // 编译器兜底：switch 已穷尽所有枚举值
}
