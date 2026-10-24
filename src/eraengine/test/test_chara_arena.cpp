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
// test_chara_arena.cpp —— CHARADATA 的「角色基址表 + 连续 arena」契约
//
// 契约（只走公开 API，与实现无关）：
//   1. **稀疏角色号不撑大槽位**：写角色 5'000'000 的元素 0 之后
//      allocatedSlots == 每角色元素数（不是 5M×元素数）；
//   2. 多角色隔离：同一变量的不同角色号各占一块，互不覆盖；
//      不同变量各有自己的 arena（互不影响）；
//   3. 读纯性：读未分配角色 → 0/空串，且**读不改变** allocatedSlots
//      （读路径不分配）；
//   4. 元素偏移越界：权威几何读 0 / 写忽略（同一条诊断），负下标不写；
//      非权威几何允许懒扩容（含「已有块随 elementCount 一起重排」）；
//   5. CSV 模板快照：稀疏模板号下快照与运行时仍互不影响，且各读各的；
//   6. 存档往返：稀疏角色号 + 普通角色的值存 → 读 → 逐项相同；
//   7. 上限：超过元素数上限的写被拒绝（同一诊断风格），容器不变。
//
// 旧实现（slot = charaSlot·elementCount + offset）在第 1 条上必红（槽位 = 5M×元素数），
// 其余条目是「换布局不许改语义」的等价护栏。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>
#include <QSet>

#include "variable_storage.h"
#include "save_variable_map.h"
#include "era_binary_data.h"
#include "ast/variable_table.h"
#include "ast/name_interner.h"

static int g_failures = 0;
static int g_assertions = 0;

static void check(bool cond, const QString& what) {
    ++g_assertions;
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

namespace {

QStringList g_warnings;
QtMessageHandler g_prevHandler = nullptr;

void captureHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (type == QtWarningMsg || type == QtCriticalMsg) g_warnings.append(msg);
    if (g_prevHandler) g_prevHandler(type, ctx, msg);
}

bool sawWarning(const QString& needle) {
    for (const QString& w : g_warnings) if (w.contains(needle)) return true;
    return false;
}

// 声明：CHARADATA 的每角色元素数由声明乘积决定（与 shapeOf 同一份事实）。
void addCharaDecl(VariableTable& table, const QString& name, const QList<int>& lengths,
                  bool stringType = false, bool savedata = false) {
    VariableDecl d;
    d.name = name;
    d.isCharaData = true;
    d.isSavedata = savedata;
    d.type = stringType ? OperandType::Str : OperandType::Int;
    d.dimension = lengths.size();
    d.lengths = lengths;
    qint64 count = 1;
    for (int n : lengths) count *= n;
    d.elementCount = count;
    table.add(d);
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    g_prevHandler = qInstallMessageHandler(captureHandler);

    // =====================================================================
    qDebug() << "\n1) 稀疏角色号：槽位只按写过的角色数增长";
    {
        VariableStorage vs;
        VariableTable table;
        addCharaDecl(table, QStringLiteral("C1"), {4});
        vs.setSaveVariableTable(&table);
        vs.registerCharaDataVariable(QStringLiteral("C1"), false, 1);
        vs.initialize(10, 10);

        const int sparse = 5'000'000;
        vs.setCharaIntRaw(QStringLiteral("C1"), sparse, 0, 431);
        check(vs.getCharaInt(QStringLiteral("C1"), sparse, 0) == 431,
              QStringLiteral("稀疏角色 %1 的元素 0 往返").arg(sparse));
        const int slotCount = vs.allocatedSlots(QStringLiteral("C1"));
        check(slotCount == 4,
              QStringLiteral("allocatedSlots == 每角色元素数 4（实得 %1，不是 %2）")
                  .arg(slotCount).arg(qint64(sparse) * 4));
        // 同一角色其余元素仍读 0（块内未写 = 默认值）。
        check(vs.getCharaInt(QStringLiteral("C1"), sparse, 3) == 0,
              QStringLiteral("同一稀疏角色的未写元素读 0"));
        vs.setCharaIntRaw(QStringLiteral("C1"), sparse, 3, 7);
        check(vs.getCharaInt(QStringLiteral("C1"), sparse, 3) == 7,
              QStringLiteral("同一稀疏角色的末元素往返"));
        check(vs.allocatedSlots(QStringLiteral("C1")) == 4,
              QStringLiteral("块内再写不追加块（allocatedSlots 仍 4）"));
    }

    // =====================================================================
    qDebug() << "\n2) 多角色隔离 + 多变量各自独立 arena";
    {
        VariableStorage vs;
        VariableTable table;
        addCharaDecl(table, QStringLiteral("C1"), {4});
        addCharaDecl(table, QStringLiteral("U1"), {2});
        vs.setSaveVariableTable(&table);
        vs.registerCharaDataVariable(QStringLiteral("C1"), false, 1);
        vs.registerCharaDataVariable(QStringLiteral("U1"), false, 1);
        vs.initialize(10, 10);

        vs.setCharaIntRaw(QStringLiteral("C1"), 7, 3, 70);
        vs.setCharaIntRaw(QStringLiteral("C1"), 9, 0, 90);
        vs.setCharaIntRaw(QStringLiteral("U1"), 7, 1, 71);
        check(vs.getCharaInt(QStringLiteral("C1"), 7, 3) == 70
                  && vs.getCharaInt(QStringLiteral("C1"), 9, 0) == 90,
              QStringLiteral("两个角色号各占一块，互不覆盖"));
        check(vs.allocatedSlots(QStringLiteral("C1")) == 8,
              QStringLiteral("C1 两块 = 8 槽（实得 %1）").arg(vs.allocatedSlots(QStringLiteral("C1"))));
        check(vs.allocatedSlots(QStringLiteral("U1")) == 2,
              QStringLiteral("U1 自己的 arena = 2 槽（实得 %1）").arg(vs.allocatedSlots(QStringLiteral("U1"))));
        check(vs.getCharaInt(QStringLiteral("C1"), 7, 0) == 0
                  && vs.getCharaInt(QStringLiteral("C1"), 9, 1) == 0,
              QStringLiteral("块之间不串味（未写元素仍是 0）"));
        check(vs.getCharaInt(QStringLiteral("U1"), 9, 1) == 0,
              QStringLiteral("变量之间不串味（U1 没写过角色 9）"));
    }

    // =====================================================================
    qDebug() << "\n3) 读纯性：未分配角色读 0/空串，读不改变 allocatedSlots";
    {
        VariableStorage vs;
        VariableTable table;
        addCharaDecl(table, QStringLiteral("C1"), {4});
        addCharaDecl(table, QStringLiteral("S1"), {3}, /*stringType=*/true);
        vs.setSaveVariableTable(&table);
        vs.registerCharaDataVariable(QStringLiteral("C1"), false, 1);
        vs.registerCharaDataVariable(QStringLiteral("S1"), true, 1);
        vs.initialize(10, 10);

        vs.setCharaIntRaw(QStringLiteral("C1"), 5, 0, 5);
        vs.setCharaStrRaw(QStringLiteral("S1"), 5, 0, QStringLiteral("five"));
        const int intSlots = vs.allocatedSlots(QStringLiteral("C1"));
        const int strSlots = vs.allocatedSlots(QStringLiteral("S1"));

        bool zero = true;
        for (int i = 0; i < 50; ++i) {
            if (vs.getCharaInt(QStringLiteral("C1"), 1000 + i * 7, i % 4) != 0) zero = false;
            if (!vs.getCharaStr(QStringLiteral("S1"), 1000 + i * 7, i % 3).isEmpty()) zero = false;
        }
        check(zero, QStringLiteral("50 个未分配角色号：读 0 / 空串"));
        check(vs.allocatedSlots(QStringLiteral("C1")) == intSlots
                  && vs.allocatedSlots(QStringLiteral("S1")) == strSlots,
              QStringLiteral("读 100 次后 allocatedSlots 不变（%1/%2）")
                  .arg(vs.allocatedSlots(QStringLiteral("C1")))
                  .arg(vs.allocatedSlots(QStringLiteral("S1"))));
        check(vs.allocatedSlots(QStringLiteral("C9")) == 0,
              QStringLiteral("从未写过的变量 allocatedSlots == 0"));
    }

    // =====================================================================
    qDebug() << "\n4) 元素偏移越界（权威几何）与懒扩容（非权威几何）";
    {
        VariableStorage vs;
        VariableTable table;
        addCharaDecl(table, QStringLiteral("C1"), {4});
        vs.setSaveVariableTable(&table);
        vs.registerCharaDataVariable(QStringLiteral("C1"), false, 1);
        vs.registerCharaDataVariable(QStringLiteral("C9"), false, 2);
        vs.initialize(10, 10);

        vs.setCharaIntRaw(QStringLiteral("C1"), 3, 3, 33);
        const int slotCount = vs.allocatedSlots(QStringLiteral("C1"));
        g_warnings.clear();
        check(vs.getCharaInt(QStringLiteral("C1"), 3, 4) == 0,
              QStringLiteral("元素偏移越界（上界）读 0"));
        vs.setCharaIntRaw(QStringLiteral("C1"), 3, 4, 44);
        check(true, QStringLiteral("元素偏移越界（上界）写被忽略"));
        check(sawWarning(QStringLiteral("越界（忽略）")) && sawWarning(QStringLiteral("C1")),
              QStringLiteral("越界写的诊断与今天同一句（角色变量 … 越界（忽略））"));
        check(vs.allocatedSlots(QStringLiteral("C1")) == slotCount,
              QStringLiteral("越界写不扩容器"));
        vs.setCharaIntRaw(QStringLiteral("C1"), 3, -1, 45);
        check(vs.getCharaInt(QStringLiteral("C1"), 3, 0) == 0,
              QStringLiteral("负下标不写（元素 0 仍是 0）"));

        // 非权威几何（未声明 C9，2 维访问）：懒扩容 + 已有块随 elementCount 重排。
        vs.setCharaInt3D(QStringLiteral("C9"), 5, 1, 2, 77);
        vs.setCharaInt3D(QStringLiteral("C9"), 3, 0, 1, 88);
        check(vs.getCharaInt3D(QStringLiteral("C9"), 5, 1, 2) == 77
                  && vs.getCharaInt3D(QStringLiteral("C9"), 3, 0, 1) == 88,
              QStringLiteral("非权威几何：两个角色的块各自往返"));
        check(vs.allocatedSlots(QStringLiteral("C9")) == 12,
              QStringLiteral("块长随几何 = 2×3 = 6（两块 12 槽，实得 %1）")
                  .arg(vs.allocatedSlots(QStringLiteral("C9"))));
        // 再扩一次边界（2×3 -> 4×5=20）：既有块必须一起重排，值不丢。
        vs.setCharaInt3D(QStringLiteral("C9"), 5, 3, 4, 99);
        check(vs.getCharaInt3D(QStringLiteral("C9"), 5, 3, 4) == 99,
              QStringLiteral("扩容后的新元素往返"));
        check(vs.getCharaInt3D(QStringLiteral("C9"), 5, 1, 2) == 77,
              QStringLiteral("扩容后旧块的值不丢（角色 5 的 (1,2) 仍 77）"));
        check(vs.getCharaInt3D(QStringLiteral("C9"), 3, 0, 1) == 88,
              QStringLiteral("扩容后另一个角色块也不丢（角色 3 的 (0,1) 仍 88）"));
        check(vs.allocatedSlots(QStringLiteral("C9")) == 40,
              QStringLiteral("扩容后两块 × 20 槽 = 40（实得 %1）")
                  .arg(vs.allocatedSlots(QStringLiteral("C9"))));
    }

    // =====================================================================
    qDebug() << "\n5) CSV 模板快照：稀疏模板号下快照与运行时各自独立";
    {
        VariableStorage vs;
        VariableTable table;
        addCharaDecl(table, QStringLiteral("C1"), {4});
        addCharaDecl(table, QStringLiteral("S1"), {3}, /*stringType=*/true);
        vs.setSaveVariableTable(&table);
        vs.registerCharaDataVariable(QStringLiteral("C1"), false, 1);
        vs.registerCharaDataVariable(QStringLiteral("S1"), true, 1);
        vs.initialize(10, 10);

        const int tmpl = 9002;   // eraMegaten 的最大模板号（稀疏）
        vs.setCharaIntRaw(QStringLiteral("C1"), tmpl, 1, 42);
        vs.setCharaStrRaw(QStringLiteral("S1"), tmpl, 0, QStringLiteral("csv-name"));
        vs.snapshotCharaTemplates();
        vs.setCharaIntRaw(QStringLiteral("C1"), tmpl, 1, 99);
        vs.setCharaStrRaw(QStringLiteral("S1"), tmpl, 0, QStringLiteral("runtime-name"));

        check(vs.getCharaInt(QStringLiteral("C1"), tmpl, 1) == 99
                  && vs.getCharaStr(QStringLiteral("S1"), tmpl, 0) == QStringLiteral("runtime-name"),
              QStringLiteral("运行时数据已被改写"));
        check(vs.csvCharaInt(QStringLiteral("C1"), tmpl, 1) == 42,
              QStringLiteral("CSV 快照按模板号读回（42）"));
        check(vs.csvCharaStr(QStringLiteral("S1"), tmpl, 0) == QStringLiteral("csv-name"),
              QStringLiteral("CSV 快照字符串按模板号读回"));
        check(vs.csvCharaInt(QStringLiteral("C1"), 12345, 1) == 0
                  && vs.csvCharaStr(QStringLiteral("S1"), 12345, 0).isEmpty(),
              QStringLiteral("快照里没有的模板号读 0 / 空串"));
        check(vs.allocatedSlots(QStringLiteral("C1")) == 4,
              QStringLiteral("快照不影响运行时 arena（C1 仍是 1 块 = 4 槽，实得 %1）")
                  .arg(vs.allocatedSlots(QStringLiteral("C1"))));
    }

    // =====================================================================
    qDebug() << "\n6) 存档往返：稀疏角色号 + 普通角色";
    {
        VariableStorage vs;
        VariableTable table;
        addCharaDecl(table, QStringLiteral("C1"), {4}, false, /*savedata=*/true);
        addCharaDecl(table, QStringLiteral("S1"), {3}, true, /*savedata=*/true);
        vs.setSaveVariableTable(&table);
        vs.registerCharaDataVariable(QStringLiteral("C1"), false, 1);
        vs.registerCharaDataVariable(QStringLiteral("S1"), true, 1);
        vs.initialize(10, 10);
        vs.appendCharacterSnapshot({{3, false}, {5000000, false}, {7, false}});

        vs.setCharaIntRaw(QStringLiteral("C1"), 3, 0, 301);
        vs.setCharaIntRaw(QStringLiteral("C1"), 5000000, 2, 5002);
        vs.setCharaIntRaw(QStringLiteral("C1"), 7, 3, 703);
        vs.setCharaStrRaw(QStringLiteral("S1"), 5000000, 1, QStringLiteral("sparse-name"));
        vs.setCharaStrRaw(QStringLiteral("S1"), 3, 0, QStringLiteral("plain"));

        auto doc = vs.captureBinarySave(EraBinaryData::FileType::Normal);
        doc.description = QStringLiteral("chara-arena");
        doc.uniqueCode = 7;
        QByteArray bytes;
        check(EraBinaryData::encode(doc, bytes) && !bytes.isEmpty(),
              QStringLiteral("存档编码成功（%1 字节）").arg(bytes.size()));

        VariableStorage loaded;
        VariableTable table2;
        addCharaDecl(table2, QStringLiteral("C1"), {4}, false, true);
        addCharaDecl(table2, QStringLiteral("S1"), {3}, true, true);
        loaded.setSaveVariableTable(&table2);
        loaded.registerCharaDataVariable(QStringLiteral("C1"), false, 1);
        loaded.registerCharaDataVariable(QStringLiteral("S1"), true, 1);
        loaded.initialize(10, 10);   // 角色列表由 applyBinarySave 自己重建

        EraBinaryData::Document decoded;
        check(EraBinaryData::decode(bytes, decoded), QStringLiteral("存档解码成功"));
        loaded.applyBinarySave(decoded);

        // 读档后角色列表由 applyBinarySave 重建为**运行时下标 0..n-1**（存档只存值，
        // 不存模板号），所以三个角色按存档顺序落在下标 0/1/2 上。
        check(loaded.getCharaInt(QStringLiteral("C1"), 0, 0) == 301,
              QStringLiteral("角色 0（模板 3）C1:0 往返"));
        check(loaded.getCharaInt(QStringLiteral("C1"), 1, 2) == 5002,
              QStringLiteral("角色 1（模板 %1）C1:2 往返").arg(5000000));
        check(loaded.getCharaInt(QStringLiteral("C1"), 2, 3) == 703,
              QStringLiteral("角色 2（模板 7）C1:3 往返"));
        check(loaded.getCharaStr(QStringLiteral("S1"), 1, 1) == QStringLiteral("sparse-name"),
              QStringLiteral("稀疏角色 S1 字符串往返"));
        check(loaded.getCharaStr(QStringLiteral("S1"), 0, 0) == QStringLiteral("plain"),
              QStringLiteral("普通角色 S1 字符串往返"));
        check(loaded.getCharaInt(QStringLiteral("C1"), 1, 0) == 0
                  && loaded.getCharaStr(QStringLiteral("S1"), 2, 0).isEmpty(),
              QStringLiteral("读档后未写元素仍是 0 / 空串"));

        // 存 -> 读 -> 存：字节逐字节相同（枚举顺序与内容都没变）。
        auto again = loaded.captureBinarySave(EraBinaryData::FileType::Normal);
        again.description = doc.description;
        again.uniqueCode = doc.uniqueCode;
        again.scriptVersion = doc.scriptVersion;
        QByteArray bytes2;
        check(EraBinaryData::encode(again, bytes2) && bytes2 == bytes,
              QStringLiteral("存 → 读 → 存 字节逐字节相同"));
    }

    // =====================================================================
    qDebug() << "\n7) 上限：超过每角色元素数上限的写被拒绝（容器不变）";
    {
        VariableStorage vs;
        VariableTable table;
        addCharaDecl(table, QStringLiteral("CBIG"), {4'000'001});   // > kMaxMultiDimElements
        addCharaDecl(table, QStringLiteral("C1"), {4});
        vs.setSaveVariableTable(&table);
        vs.registerCharaDataVariable(QStringLiteral("CBIG"), false, 1);
        vs.registerCharaDataVariable(QStringLiteral("C1"), false, 1);
        vs.initialize(10, 10);

        g_warnings.clear();
        vs.setCharaIntRaw(QStringLiteral("CBIG"), 11, 0, 1);
        check(sawWarning(QStringLiteral("超过上限")), QStringLiteral("超上限的写留一条诊断"));
        check(vs.allocatedSlots(QStringLiteral("CBIG")) == 0,
              QStringLiteral("超上限不分配（allocatedSlots == 0，实得 %1）")
                  .arg(vs.allocatedSlots(QStringLiteral("CBIG"))));
        check(vs.getCharaInt(QStringLiteral("CBIG"), 11, 0) == 0,
              QStringLiteral("超上限的写没有落库"));

        // 越界写的诊断文本逐字（契约：与今天同形）。
        g_warnings.clear();
        vs.setCharaIntRaw(QStringLiteral("C1"), 0, 9, 1);
        QString hit;
        for (const QString& w : g_warnings)
            if (w.contains(QStringLiteral("角色变量")) && w.contains(QStringLiteral("越界（忽略）"))) hit = w;
        check(hit.contains(QStringLiteral("[var] 角色变量"))
                  && hit.contains(QStringLiteral("chara=")) && hit.contains(QStringLiteral("元素=")),
              QStringLiteral("越界写诊断逐项在场：%1").arg(hit));
    }

    qInstallMessageHandler(g_prevHandler);
    qDebug().noquote() << "\n================================";
    qDebug().noquote() << "断言数:" << g_assertions;
    if (g_failures == 0) qDebug().noquote() << "ALL PASS";
    else qDebug().noquote() << "FAILURES:" << g_failures;
    return g_failures == 0 ? 0 : 1;
}
