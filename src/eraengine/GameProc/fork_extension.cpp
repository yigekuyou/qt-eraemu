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
// ---------------------------------------------------------------------------
// fork_extension.cpp —— EM / Emuera.NET fork 扩展（「仿照语义」自研实现）
//
// 本文件：共享工具（ref 数组输出） + MAP_* + ENUM* + CALLSHARP（**桩**：见下）
//         + registerForkExtensions（并转交 fork_xml / fork_datatable）。
//
// 语义来源（逐条对齐；不读 C# 也能照 test/data/functions/*.md 验收）：
//   emuera.em/Emuera/Runtime/Script/Statements/Function/Creator.Method.cs
//   emuera.em/Emuera/Runtime/Script/Statements/Function/Creator.cs（methodList）
//   emuera.em/Emuera/Runtime/Utils/PluginSystem/*（CALLSHARP / IPluginMethod）
//
// 「仿照语义」的含义：这些命令**不是**引擎内建，而是 .NET 运行期能力的封装
//   （Dictionary / XmlDocument+XPath / DataTable / 反射）。本移植不引入数据库、
//   不引入第三方 XML 库：按文档语义**自己实现等价物**，只用已链接的 Qt6::Core。
// ---------------------------------------------------------------------------

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStringList>

#include "fork_extension.h"
#include "fork_support.h"
#include "extension_registry.h"
#include "expression_ast.h"
#include "variable_storage.h"

namespace forkxml { void registerXmlExtensions(ExtensionRegistry& ext); }
namespace forkdt  { void registerDataTableExtensions(ExtensionRegistry& ext); }

// ===========================================================================
// 共享工具（fork_support.h 的实现）
// ===========================================================================
namespace forksupport {

QString refTargetName(const ExpressionNode* node) {
    if (node == nullptr || node->kind() != NodeKind::Variable) return QString();
    return static_cast<const VariableNode*>(node)->name();
}

int arrayCapacity(VariableStorage* storage, const QString& name) {
    if (storage == nullptr || name.isEmpty()) return 0;
    const int cfg = storage->variableConfig().getSize1D(name);
    if (cfg > 0) return cfg;
    const int live = storage->arraySize(name);
    return live > 0 ? live : 0;
}

void writeInt(VariableStorage* storage, const QString& name, int index, qint64 value) {
    if (storage == nullptr || name.isEmpty() || index < 0) return;
    if (storage->hasSystemVariable(name)) {
        storage->setSystemVariable(name, index, value);
        return;
    }
    storage->setGlobalInt1D(name, index, value);
}

void writeStr(VariableStorage* storage, const QString& name, int index, const QString& value) {
    if (storage == nullptr || name.isEmpty() || index < 0) return;
    if (storage->hasSystemVariable(name)) {
        storage->setSystemStr(name, index, value);
        return;
    }
    storage->setGlobalStr1D(name, index, value);
}

bool matchesPrefix(const QString& name, const QString& pattern) {
    return name.size() >= pattern.size()
        && name.left(pattern.size()).compare(pattern, Qt::CaseInsensitive) == 0;
}

bool matchesSuffix(const QString& name, const QString& pattern) {
    return name.size() >= pattern.size()
        && name.right(pattern.size()).compare(pattern, Qt::CaseInsensitive) == 0;
}

bool matchesContains(const QString& name, const QString& pattern) {
    return name.contains(pattern, Qt::CaseInsensitive);
}

} // namespace forksupport

namespace {

using namespace forksupport;

// ===========================================================================
// MAP_* —— 字符串关联数组（C# VariableData.DataStringMaps: Dictionary<string,string>）
//
// Creator.cs:265-278；实现 Creator.Method.cs:1780-1960。
//
// 顺序：C# 用 Dictionary，实际表现为「无删除时的插入顺序」。为可复现，这里用
// **插入有序**容器（删除后再插入排到末尾）—— 与文档「顺序不保证、需要时自行排序」
// 一致，但比 QHash 的任意顺序更接近 .NET 实际表现，测试也才能确定断言。
// ===========================================================================
struct OrderedMap {
    QStringList keys;                 // 插入顺序
    QHash<QString, QString> values;

    void set(const QString& k, const QString& v) {
        if (!values.contains(k)) keys.append(k);
        values.insert(k, v);
    }
    [[nodiscard]] bool has(const QString& k) const { return values.contains(k); }
    [[nodiscard]] QString get(const QString& k) const { return values.value(k); }
    void remove(const QString& k) { if (values.remove(k) > 0) keys.removeAll(k); }
    void clear() { keys.clear(); values.clear(); }
    [[nodiscard]] int size() const { return values.size(); }
};

// 进程内单例（对齐 C# VariableData.DataStringMaps：随变量数据走的全局容器）
QHash<QString, OrderedMap>& mapStore() {
    static QHash<QString, OrderedMap> store;
    return store;
}

// MAP_TOXML 的确切格式（无换行、无缩进、**不做 XML 转义** —— 对齐 C# 源码）
QString mapToXml(const OrderedMap& m) {
    QString out = QStringLiteral("<map>");
    for (const QString& k : m.keys) {
        out += QStringLiteral("<p><k>") + k + QStringLiteral("</k><v>")
             + m.values.value(k) + QStringLiteral("</v></p>");
    }
    out += QStringLiteral("</map>");
    return out;
}

// MAP_FROMXML：从 "<map><p><k>k</k><v>v</v></p>…</map>" 读入。
// 对齐 C#：以 XPath "/map/p" 选节点，每个 <p> 下 <k>/<v> **各恰好一个**才处理；
// 键取 InnerText、值取 InnerXml。这里用最小手写扫描（不引入 XML 库）。
bool mapFromXml(const QString& xml, OrderedMap& out) {
    const QString trimmed = xml.trimmed();
    if (!trimmed.startsWith(QLatin1String("<map"))) return false;
    int pos = 0;
    while (true) {
        const int pBegin = trimmed.indexOf(QLatin1String("<p>"), pos);
        if (pBegin < 0) break;
        const int pEnd = trimmed.indexOf(QLatin1String("</p>"), pBegin);
        if (pEnd < 0) break;
        const QString body = trimmed.mid(pBegin + 3, pEnd - pBegin - 3);
        pos = pEnd + 4;

        const int kBegin = body.indexOf(QLatin1String("<k>"));
        const int kEnd = body.indexOf(QLatin1String("</k>"));
        const int vBegin = body.indexOf(QLatin1String("<v>"));
        const int vEnd = body.indexOf(QLatin1String("</v>"));
        if (kBegin < 0 || kEnd < 0 || vBegin < 0 || vEnd < 0) continue;
        if (body.indexOf(QLatin1String("<k>"), kBegin + 3) >= 0) continue;   // 必须恰好 1 个
        if (body.indexOf(QLatin1String("<v>"), vBegin + 3) >= 0) continue;
        out.set(body.mid(kBegin + 3, kEnd - kBegin - 3),
                body.mid(vBegin + 3, vEnd - vBegin - 3));
    }
    return true;
}

// ===========================================================================
// ENUM* —— 名字枚举（Creator.cs:226-235；实现 Creator.Method.cs:151-260）
//
// 匹配一律 ToUpper() 后比较（不区分大小写），输出保持原样、**不排序**；
// 空前缀不匹配任何名字 -> 0；返回值是**写入个数**（受目标数组容量限制）。
// 目标：第 2 参给定 = 该字符串数组，省略 = 系统数组 RESULTS。
// ===========================================================================
enum class EnumAction { BeginsWith, EndsWith, With };

qint64 enumNames(const QStringList& names, const QString& pattern, EnumAction action,
                 const QList<const ExpressionNode*>& argNodes, VariableStorage* storage) {
    if (pattern.isEmpty()) return 0;                        // 空前缀不枚举

    QStringList hits;
    for (const QString& n : names) {
        if (n.size() < pattern.size()) continue;
        const bool ok = action == EnumAction::BeginsWith ? matchesPrefix(n, pattern)
                      : action == EnumAction::EndsWith   ? matchesSuffix(n, pattern)
                                                         : matchesContains(n, pattern);
        if (ok) hits.append(n);
    }

    const QString target = argNodes.size() >= 2 ? refTargetName(argNodes.at(1)) : QString();
    const QString outName = target.isEmpty() ? QStringLiteral("RESULTS") : target;
    const int cap = arrayCapacity(storage, outName);
    const int count = cap > 0 ? qMin(hits.size(), cap) : hits.size();
    for (int i = 0; i < count; ++i) writeStr(storage, outName, i, hits.at(i));
    return count;
}

// 注册一族 ENUM（3 个动作 × 3 个名字集）
void registerEnumFamily(ExtensionRegistry& ext, const QString& base,
                        std::function<QStringList()> namesProvider)
{
    struct Entry { const char* suffix; EnumAction action; };
    const Entry entries[] = {
        { "BEGINSWITH", EnumAction::BeginsWith },
        { "ENDSWITH",   EnumAction::EndsWith },
        { "WITH",       EnumAction::With },
    };
    for (const Entry& e : entries) {
        const QString name = base + QLatin1String(e.suffix);
        const EnumAction action = e.action;
        ext.regExpr(name, OperandType::Int, 1, 2,
            [&ext, namesProvider, action](const QList<QVariant>& a,
                                          const QList<const ExpressionNode*>& nodes,
                                          QVariant& out) {
                const QString pattern = a.value(0).toString();
                const QStringList names = namesProvider ? namesProvider() : QStringList();
                const qint64 n = enumNames(names, pattern, action, nodes,
                                           ext.services().storage);
                out = QVariant::fromValue<qint64>(n);
                return true;
            });
    }
}

// ===========================================================================
// CALLSHARP —— **本移植不实现**（保持「名字容忍」桩）
//
// C# 侧（emuera.em 私有扩展）：启动时 Assembly.LoadFrom 装载 `<exe 目录>/Plugins/*.dll`，
//   取其中的 `PluginManifest` 类型，其 GetPluginMethods() 给出 IPluginMethod[]；
//   `CALLSHARP "名字"(实参…)` 按名字调用，实参是变量时把插件改过的值回写。
//
// 为什么**标记为不可能实现**（而不是照抄一份）：
//   1. **跨平台**：本移植是 Qt6 + QML/C++ 的跨平台项目（Linux/Windows/macOS/Android）。
//      「装载本机共享库」这一步本身就要平台相关代码：Windows 是 `LoadLibrary`、
//      Linux 是 `dlopen`（`QLibrary` 也只是对它们的薄封装），Android 还涉及
//      APK 内 .so 的打包/加载路径与权限模型 —— 与本项目「一份源码多平台出包」的
//      取向冲突（.github/workflows/build.yml 三平台 + Android APK）。
//   2. **目标不可达**：C# 插件是**托管程序集**，任何 C/C++ 宿主都无法直接执行
//      （需要 .NET/CLR）。也就是说「照原样兼容」在这条技术路线上根本做不到，
//      只能另立一套原生 ABI —— 而那已不是 CALLSHARP 的语义（存量插件一个都跑不了）。
//   3. **能力面**：该功能在 .NET 侧被明确标为「危险功能」（源码多处 `#region
//      EE_CALLSHARP注意`），且仓库里的样例游戏（eraTW / eraMegaten）**零使用**。
//
// 因此按仓库的桩政策处理：**只登记名字**（装载/运行期不报「未知命令」，
// 运行期静默跳过），并在 test/change/commands.md 的 CALLSHARP 节里注明上述原因。
// ===========================================================================
} // namespace

// ---------------------------------------------------------------------------
// 注册入口（只由 ExtensionRegistry 构造函数调用）
// ---------------------------------------------------------------------------
void registerForkExtensions(ExtensionRegistry& ext)
{
    // ---------------- MAP_* （12 条，Creator.cs:265-278） ----------------
    ext.regExpr(QStringLiteral("MAP_CREATE"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const QString key = a.value(0).toString();
            auto& store = mapStore();
            if (store.contains(key)) { out = QVariant::fromValue<qint64>(0); return true; }
            store.insert(key, OrderedMap{});
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    ext.regExpr(QStringLiteral("MAP_EXIST"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            out = QVariant::fromValue<qint64>(mapStore().contains(a.value(0).toString()) ? 1 : 0);
            return true;
        });

    ext.regExpr(QStringLiteral("MAP_RELEASE"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            mapStore().remove(a.value(0).toString());      // C#：存在或不存在都返回 1
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    ext.regExpr(QStringLiteral("MAP_SET"), OperandType::Int, 3, 3,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = mapStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            store[key].set(a.value(1).toString(), a.value(2).toString());
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    ext.regExpr(QStringLiteral("MAP_HAS"), OperandType::Int, 2, 2,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = mapStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            out = QVariant::fromValue<qint64>(store[key].has(a.value(1).toString()) ? 1 : 0);
            return true;
        });

    ext.regExpr(QStringLiteral("MAP_REMOVE"), OperandType::Int, 2, 2,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = mapStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            store[key].remove(a.value(1).toString());
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    ext.regExpr(QStringLiteral("MAP_CLEAR"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = mapStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            store[key].clear();
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    ext.regExpr(QStringLiteral("MAP_SIZE"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = mapStore();
            const QString key = a.value(0).toString();
            out = QVariant::fromValue<qint64>(store.contains(key) ? store[key].size() : -1);
            return true;
        });

    ext.regExpr(QStringLiteral("MAP_GET"), OperandType::Str, 2, 2,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = mapStore();
            const QString key = a.value(0).toString();
            out = store.contains(key) ? store[key].get(a.value(1).toString()) : QString();
            return true;
        });

    ext.regExpr(QStringLiteral("MAP_TOXML"), OperandType::Str, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = mapStore();
            const QString key = a.value(0).toString();
            out = store.contains(key) ? mapToXml(store[key]) : QString();
            return true;
        });

    ext.regExpr(QStringLiteral("MAP_FROMXML"), OperandType::Int, 2, 2,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = mapStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(0); return true; }  // C#：不存在 -> 0
            if (!mapFromXml(a.value(1).toString(), store[key])) {
                out = QVariant::fromValue<qint64>(0);   // C# 抛 CodeEE；容错实现：按失败记 0
                return true;
            }
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    // MAP_GETKEYS 三形态（Creator.Method.cs:1847）：
    //   1 参 -> 逗号拼接串（不写 RESULT/RESULTS）
    //   2 参 -> 第 2 参非 0：键写 RESULTS、个数写 RESULT，返回 RESULTS:0
    //   3 参 -> 第 3 参非 0：键写第 2 参数组、个数写 RESULT，返回 ""
    ext.regExpr(QStringLiteral("MAP_GETKEYS"), OperandType::Str, 1, 3,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
               QVariant& out) {
            auto& store = mapStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QString(); return true; }
            const OrderedMap& m = store[key];
            VariableStorage* st = ext.services().storage;

            if (a.size() == 1) {                       // 形式 1：逗号拼接
                out = m.keys.join(QLatin1Char(','));
                return true;
            }
            if (a.size() == 2) {                       // 形式 2：RESULTS + RESULT
                if (a.value(1).toLongLong() == 0) { out = QString(); return true; }
                const int cap = arrayCapacity(st, QStringLiteral("RESULTS"));
                const int n = cap > 0 ? qMin(m.keys.size(), cap) : m.keys.size();
                for (int i = 0; i < n; ++i) writeStr(st, QStringLiteral("RESULTS"), i, m.keys.at(i));
                writeInt(st, QStringLiteral("RESULT"), 0, m.keys.size());
                out = n > 0 ? m.keys.at(0) : QString();
                return true;
            }
            // 形式 3：调用者数组 + RESULT
            if (a.value(2).toLongLong() == 0) { out = QString(); return true; }
            const QString target = nodes.size() >= 2 ? refTargetName(nodes.at(1)) : QString();
            const int cap = arrayCapacity(st, target);
            const int n = cap > 0 ? qMin(m.keys.size(), cap) : m.keys.size();
            for (int i = 0; i < n; ++i) writeStr(st, target, i, m.keys.at(i));
            writeInt(st, QStringLiteral("RESULT"), 0, m.keys.size());
            out = QString();
            return true;
        });

    // ---------------- ENUM* （10 条，Creator.cs:226-235） ----------------
    registerEnumFamily(ext, QStringLiteral("ENUMFUNC"),
                       [&ext] { return ext.services().enumFunctionNames
                                       ? ext.services().enumFunctionNames() : QStringList(); });
    registerEnumFamily(ext, QStringLiteral("ENUMVAR"),
                       [&ext] { return ext.services().enumVariableNames
                                       ? ext.services().enumVariableNames() : QStringList(); });
    registerEnumFamily(ext, QStringLiteral("ENUMMACRO"),
                       [&ext] { return ext.services().enumMacroNames
                                       ? ext.services().enumMacroNames() : QStringList(); });

    // ENUMFILES（Creator.Method.cs:223）：目录枚举，相对 exeDir 输出，失败 -1
    ext.regExpr(QStringLiteral("ENUMFILES"), OperandType::Int, 1, 4,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
               QVariant& out) {
            const ExtensionRegistry::Services& sv = ext.services();
            const QString base = sv.exeDir ? sv.exeDir() : QString();
            QString dir = a.value(0).toString();
            // Utils.GetValidPath：绝对路径直接拒绝；'..' 段拒绝；'/' 归一为 '/'
            if (dir.isEmpty() || QDir::isAbsolutePath(dir) || dir.contains(QLatin1String(".."))) {
                out = QVariant::fromValue<qint64>(-1);
                return true;
            }
            const QString pattern = a.size() > 1 ? a.value(1).toString() : QStringLiteral("*");
            const bool recursive = a.size() > 2 && a.value(2).toLongLong() != 0;
            const QDir root(base);
            const QDir target(root.absoluteFilePath(dir));
            if (!target.exists()) { out = QVariant::fromValue<qint64>(-1); return true; }

            QStringList result;
            QDirIterator it(target.absolutePath(), QStringList{ pattern }, QDir::Files,
                            recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
            while (it.hasNext()) {
                it.next();
                result.append(root.relativeFilePath(it.filePath()));
            }
            const QString targetName = nodes.size() >= 4 ? refTargetName(nodes.at(3)) : QString();
            const QString outName = targetName.isEmpty() ? QStringLiteral("RESULTS") : targetName;
            const int cap = arrayCapacity(sv.storage, outName);
            const int n = cap > 0 ? qMin(result.size(), cap) : result.size();
            for (int i = 0; i < n; ++i) writeStr(sv.storage, outName, i, result.at(i));
            out = QVariant::fromValue<qint64>(n);
            return true;
        });

    // ---------------- CALLSHARP：**保持桩**（原因见文件中部注释） ----------------
    // 只登记名字 -> 「留痕一次 + 跳过」（装载/运行期不报未知命令，运行期静默跳过）。
    ext.reg(QStringLiteral("CALLSHARP"));

    // ---------------- 转交：XML_* / DT_* ----------------
    forkxml::registerXmlExtensions(ext);
    forkdt::registerDataTableExtensions(ext);
}
