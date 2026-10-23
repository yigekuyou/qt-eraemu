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
// test_string_locus.cpp —— 「名字 -> 字符串存储」的解析顺序护栏
//
// getStringSlot / setStringSlot / getStringSlotRaw 各自手写过一遍
// 「LOCALS / ARGS / SAVEDATA_TEXT / CHARADATA / 普通扁平槽」的分派；
// 读写两份必须永远同步，否则就是「写得进、读不出」（编译期不报）。
// 这一组断言只走公开 API，与解析实现形态无关：
//
//   1. LOCALS / ARGS：按下标读写往返；写 size 之外的下标会懒扩容；
//   2. SAVEDATA_TEXT：单槽（任意下标同一格，写不同下标互相覆盖）；
//      且 SAVEDATA_TEXT 行先于 CHARADATA 行（同名重叠时，行序即语义）；
//   3. CHARADATA 字符串：按角色分块（多角色隔离）、元素偏移、
//      整型视图读 0 / 写 no-op；
//   4. 普通字符串变量：1D 与 2D/3D 同一格；越界读空、越界写不崩（忽略）；
//   5. FlatStringValue 的 markup 分片：getStringSlot 与 getStringSlotRaw
//      都必须原样带回（Raw 存在的理由，不能被去重吃掉）；
//   6. getStringSlotRaw 的「键已解析」性质：A/B 两个私有作用域同名变量，
//      传哪个键读哪个，**不重新按 scope 解析**（键文本挂 REF 别名也不改道）；
//   7. 负下标：读空 / 写 false，两侧都「没有发生」（不做扩容、不建槽）。
//
// 这一组先在旧实现（三个入口各一份 if 链）上全绿；把分派收敛成
// resolveStrLocus + 两个穷尽 switch 之后，断言条数与结果必须完全一致。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QLatin1String>
#include <QList>
#include <QString>

#include "ast/name_interner.h"
#include "variable_storage.h"

static int g_failures = 0;
static int g_assertions = 0;

static void check(bool cond, const QString& what) {
    ++g_assertions;
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static FlatStringValue sv(const QString& text) { return FlatStringValue(text); }

// 带 markup 分片的值（HTML_BUTTON 的形态：一个按钮分片 + 其显示文本）。
static FlatStringValue marked(const QString& text, const QString& buttonValue) {
    PrintTemplatePart part;
    part.kind = PrintTemplatePart::Kind::Button;
    part.text = text;
    part.buttonValue = buttonValue;
    PrintTemplate template_;
    template_.parts.append(part);
    return FlatStringValue(text, template_);
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const eravar::ScopeId global = eravar::kGlobalScope;

    // =====================================================================
    qDebug() << "\n1) LOCALS / ARGS：读写往返 + 懒扩容";
    {
        VariableStorage vs;
        const int localBefore = vs.localStrSize();
        vs.setStringSlot(QStringLiteral("LOCALS"), {localBefore + 3}, sv(QStringLiteral("L")));
        check(vs.localStrSize() == localBefore + 4,
              QStringLiteral("LOCALS: 写 size 之外的下标后容器长到下标 +1"));
        check(vs.getStringSlot(QStringLiteral("LOCALS"), {localBefore + 3}).text == QStringLiteral("L"),
              QStringLiteral("LOCALS: 读该格命中刚写的值"));
        check(vs.getStringSlot(QStringLiteral("LOCALS"), {localBefore + 2}).text.isEmpty(),
              QStringLiteral("LOCALS: 扩容出来的中间格子是空串"));
        check(vs.getLocalStr(localBefore + 3) == QStringLiteral("L"),
              QStringLiteral("LOCALS: 公开 getLocalStr 读到同一格"));

        const int argBefore = vs.arraySize(QStringLiteral("ARGS"));
        vs.setStringSlot(QStringLiteral("ARGS"), {argBefore + 2}, sv(QStringLiteral("A")));
        check(vs.arraySize(QStringLiteral("ARGS")) == argBefore + 3,
              QStringLiteral("ARGS: 写 size 之外的下标后容器长到下标 +1"));
        check(vs.getStringSlot(QStringLiteral("ARGS"), {argBefore + 2}).text == QStringLiteral("A"),
              QStringLiteral("ARGS: 读该格命中刚写的值"));
        check(vs.getArgStr(argBefore + 2) == QStringLiteral("A"),
              QStringLiteral("ARGS: 公开 getArgStr 读到同一格"));

        // 两套槽位互不干扰（eraTW 的 PRINT_COLORBAR 踩过混用）。
        check(vs.getStringSlot(QStringLiteral("ARGS"), {localBefore + 3}).text.isEmpty()
                  && vs.getStringSlot(QStringLiteral("LOCALS"), {argBefore + 2}).text.isEmpty(),
              QStringLiteral("LOCALS 与 ARGS 是两套独立容器"));
    }

    // =====================================================================
    qDebug() << "\n2) SAVEDATA_TEXT：单槽 + 行序先于 CHARADATA";
    {
        VariableStorage vs;
        vs.setStringSlot(QStringLiteral("SAVEDATA_TEXT"), {0}, sv(QStringLiteral("first")));
        vs.setStringSlot(QStringLiteral("SAVEDATA_TEXT"), {7}, sv(QStringLiteral("second")));
        check(vs.getStringSlot(QStringLiteral("SAVEDATA_TEXT"), {0}).text == QStringLiteral("second"),
              QStringLiteral("SAVEDATA_TEXT: 写下标 7 覆盖了下标 0（单槽）"));
        check(vs.getStringSlot(QStringLiteral("SAVEDATA_TEXT"), {7}).text == QStringLiteral("second"),
              QStringLiteral("SAVEDATA_TEXT: 任意下标都读到同一格"));
        check(vs.getStringSlot(QStringLiteral("SAVEDATA_TEXT"), {}).text == QStringLiteral("second"),
              QStringLiteral("SAVEDATA_TEXT: 无下标读到同一格"));
        check(vs.getSystemStr(QStringLiteral("SAVEDATA_TEXT"), 0) == QStringLiteral("second"),
              QStringLiteral("SAVEDATA_TEXT: 公开 getSystemStr 与按名读同源"));

        // 重叠名：同一个名字既在 SAVEDATA_TEXT 名单、又被登记成 CHARADATA 字符串。
        // 今天的行序是 SAVEDATA_TEXT 先命中（M1 把 CHARADATA 提前就会红）。
        VariableStorage overlap;
        overlap.registerCharaDataVariable(QStringLiteral("SAVEDATA_TEXT"), true, 0);
        overlap.setStringSlot(QStringLiteral("SAVEDATA_TEXT"), {0}, sv(QStringLiteral("sys")));
        check(overlap.getStringSlot(QStringLiteral("SAVEDATA_TEXT"), {0}).text == QStringLiteral("sys"),
              QStringLiteral("SAVEDATA_TEXT/CHARADATA: 读先命中单槽行（sys）"));
        check(overlap.getCharaStr(QStringLiteral("SAVEDATA_TEXT"), 0, 0).isEmpty(),
              QStringLiteral("SAVEDATA_TEXT/CHARADATA: 角色槽没被写（空）"));
    }

    // =====================================================================
    qDebug() << "\n3) CHARADATA 字符串：角色分块 / 元素偏移 / 整型视图";
    {
        VariableStorage vs;
        vs.registerCharaDataVariable(QStringLiteral("XS"), true, 1);
        vs.registerCharaDataVariable(QStringLiteral("XS2"), true, 2);
        vs.addChara(10);
        vs.addChara(11);

        vs.setStringSlot(QStringLiteral("XS"), {0, 0}, sv(QStringLiteral("c0")));
        vs.setStringSlot(QStringLiteral("XS"), {1, 0}, sv(QStringLiteral("c1")));
        check(vs.getStringSlot(QStringLiteral("XS"), {0, 0}).text == QStringLiteral("c0")
                  && vs.getStringSlot(QStringLiteral("XS"), {1, 0}).text == QStringLiteral("c1"),
              QStringLiteral("XS: 两个角色的字符串互不干扰"));
        check(vs.getCharaStr(QStringLiteral("XS"), 0, 0) == QStringLiteral("c0"),
              QStringLiteral("XS: 公开 getCharaStr 与按名读同源"));

        // 元素偏移（每角色 2x2 个元素）：同角色不同元素、同元素不同角色都隔离。
        // 先写最远的元素，让「运行期外接几何」一次长到 2x2；之后各格都在边界内
        //（外接几何按 indices 扩边界的重排细节不是本护栏的对象）。
        vs.setStringSlot(QStringLiteral("XS2"), {0, 1, 1}, sv(QStringLiteral("c0e11")));
        vs.setStringSlot(QStringLiteral("XS2"), {0, 0, 0}, sv(QStringLiteral("c0e00")));
        vs.setStringSlot(QStringLiteral("XS2"), {1, 0, 0}, sv(QStringLiteral("c1e00")));
        check(vs.getStringSlot(QStringLiteral("XS2"), {0, 0, 0}).text == QStringLiteral("c0e00")
                  && vs.getStringSlot(QStringLiteral("XS2"), {0, 1, 1}).text == QStringLiteral("c0e11")
                  && vs.getStringSlot(QStringLiteral("XS2"), {1, 0, 0}).text == QStringLiteral("c1e00"),
              QStringLiteral("XS2: 角色 0/1 与元素 (0,0)/(1,1) 各占一格"));

        // 字符串变量经整型视图：读恒 0、写 no-op（与 test_name_locus 契约 3 对齐）。
        check(vs.readIntByName(QStringLiteral("XS2"), {0, 0, 0}, global) == 0,
              QStringLiteral("XS2: 整型读恒 0"));
        vs.writeIntByName(QStringLiteral("XS2"), {0, 0, 0}, 77, global);
        check(vs.getStringSlot(QStringLiteral("XS2"), {0, 0, 0}).text == QStringLiteral("c0e00"),
              QStringLiteral("XS2: 整型写没有碰角色字符串槽"));
        check(vs.getGlobalInt1D(QStringLiteral("XS2"), 0) == 0,
              QStringLiteral("XS2: 整型写没有落到同名 1D 槽"));
    }

    // =====================================================================
    qDebug() << "\n4) 普通字符串变量：1D / 2D / 3D 往返；越界读空、越界写忽略";
    {
        VariableStorage vs;
        vs.installMultiDimArray(QStringLiteral("ZS"), {2, 3}, {3, 1}, /*stringArray=*/true, true);
        vs.installMultiDimArray(QStringLiteral("ZS3"), {2, 2, 2}, {4, 2, 1}, /*stringArray=*/true, true);

        vs.setStringSlot(QStringLiteral("PLAIN"), {4}, sv(QStringLiteral("p4")));
        check(vs.getStringSlot(QStringLiteral("PLAIN"), {4}).text == QStringLiteral("p4"),
              QStringLiteral("PLAIN: 1D 往返命中"));
        check(vs.getStringSlot(QStringLiteral("PLAIN"), {3}).text.isEmpty(),
              QStringLiteral("PLAIN: 相邻格仍是空串"));

        vs.setStringSlot(QStringLiteral("ZS"), {1, 2}, sv(QStringLiteral("z12")));
        check(vs.getStringSlot(QStringLiteral("ZS"), {1, 2}).text == QStringLiteral("z12"),
              QStringLiteral("ZS: 2D 往返命中同一格"));
        check(vs.getStringSlot(QStringLiteral("ZS"), {1, 2}).text
                  == vs.getStringSlot(QStringLiteral("ZS"), {5}).text,
              QStringLiteral("ZS: 扁平下标 5 与 (1,2) 是同一格"));

        vs.setStringSlot(QStringLiteral("ZS3"), {1, 1, 1}, sv(QStringLiteral("q111")));
        check(vs.getStringSlot(QStringLiteral("ZS3"), {1, 1, 1}).text == QStringLiteral("q111"),
              QStringLiteral("ZS3: 3D 往返命中同一格"));

        // 权威几何越界：读空；写被忽略（qWarning 一条）但 setStringSlot 的返回值
        // 仍是 true —— 今天 set 的 bool 只表示「下标非负」，钉住这个事实。
        check(vs.getStringSlot(QStringLiteral("ZS"), {5, 0}).text.isEmpty(),
              QStringLiteral("ZS: 越界读返回空串"));
        check(vs.setStringSlot(QStringLiteral("ZS"), {5, 0}, sv(QStringLiteral("oob"))),
              QStringLiteral("ZS: 越界写返回 true（bool 只表示下标非负）"));
        check(vs.getStringSlot(QStringLiteral("ZS"), {5, 0}).text.isEmpty(),
              QStringLiteral("ZS: 越界写被忽略（读回仍是空串）"));
        check(vs.getStringSlot(QStringLiteral("ZS"), {0, 0}).text.isEmpty(),
              QStringLiteral("ZS: 越界写没有落到别的格子"));
    }

    // =====================================================================
    qDebug() << "\n5) markup 分片：getStringSlot / getStringSlotRaw 都不丢";
    {
        VariableStorage vs;
        const FlatStringValue value = marked(QStringLiteral("按钮"), QStringLiteral("7"));
        vs.setStringSlot(QStringLiteral("MK"), {2}, value);
        const FlatStringValue read = vs.getStringSlot(QStringLiteral("MK"), {2});
        check(read.text == value.text && read.markup.parts.size() == 1
                  && read.markup.parts.at(0).kind == PrintTemplatePart::Kind::Button
                  && read.markup.parts.at(0).buttonValue == QStringLiteral("7"),
              QStringLiteral("MK: getStringSlot 带回 markup.parts"));
        const FlatStringValue raw = vs.getStringSlotRaw(QStringLiteral("MK"), {2});
        check(raw.text == value.text && raw.markup.parts.size() == 1
                  && raw.markup.parts.at(0).buttonValue == QStringLiteral("7"),
              QStringLiteral("MK: getStringSlotRaw 带回 markup.parts"));

        // 特殊槽也一样（LOCALS / ARGS 的值是原地存的 FlatStringValue）。
        vs.setStringSlot(QStringLiteral("LOCALS"), {0}, value);
        vs.setStringSlot(QStringLiteral("ARGS"), {0}, value);
        check(vs.getStringSlot(QStringLiteral("LOCALS"), {0}).markup.parts.size() == 1
                  && vs.getStringSlotRaw(QStringLiteral("LOCALS"), {0}).markup.parts.at(0).buttonValue
                         == QStringLiteral("7")
                  && vs.getStringSlotRaw(QStringLiteral("ARGS"), {0}).text == value.text,
              QStringLiteral("LOCALS/ARGS: 按名与 Raw 都保留 markup 分片"));
    }

    // =====================================================================
    qDebug() << "\n6) getStringSlotRaw 吃「已解析键」：不重新按 scope 解析";
    {
        VariableStorage vs;
        const eravar::ScopeId sidA = eravar::internName(QStringLiteral("STRLOCUS_A"));
        const eravar::ScopeId sidB = eravar::internName(QStringLiteral("STRLOCUS_B"));
        vs.setPrivateScope(sidA, {QStringLiteral("S")});
        vs.setPrivateScope(sidB, {QStringLiteral("S")});
        const QString keyA = vs.resolvedStorageName(QStringLiteral("S"), sidA);
        const QString keyB = vs.resolvedStorageName(QStringLiteral("S"), sidB);
        check(keyA != keyB, QStringLiteral("私有键: A/B 两个作用域的同名变量键不同"));

        vs.setStringSlot(QStringLiteral("S"), {0}, sv(QStringLiteral("in-A")), sidA);
        vs.setStringSlot(QStringLiteral("S"), {0}, sv(QStringLiteral("in-B")), sidB);
        check(vs.getStringSlot(QStringLiteral("S"), {0}, sidA).text == QStringLiteral("in-A")
                  && vs.getStringSlot(QStringLiteral("S"), {0}, sidB).text == QStringLiteral("in-B"),
              QStringLiteral("私有名: 按显式 scope 读写各自命中"));
        check(vs.getStringSlotRaw(keyA, {0}).text == QStringLiteral("in-A")
                  && vs.getStringSlotRaw(keyB, {0}).text == QStringLiteral("in-B"),
              QStringLiteral("Raw: 传哪个键读哪个（两槽互不干扰）"));

        // 当前帧换成 B，再拿 A 的键去读：Raw 不得把键当成名字按 m_privateScopeId
        // 重新解析，否则读到的就是 B 的槽。
        vs.setPrivateScope(sidB, {});
        check(vs.getStringSlotRaw(keyA, {0}).text == QStringLiteral("in-A"),
              QStringLiteral("Raw: 当前帧是 B 时用 A 的键仍读到 A"));

        // 更狠的一条：把「键文本本身」挂一条 REF 别名。storageName(键文本) 会
        // 改道到 HIJACK —— 任何「把已解析键再当名字解析一次」的实现都会读到
        // HIJACK 而不是本槽，这条断言就会红。
        vs.setReference(keyA, QStringLiteral("STRLOCUS_HIJACK"));
        vs.setGlobalStr1D(QStringLiteral("STRLOCUS_HIJACK"), 0, QStringLiteral("hijacked"));
        check(vs.getStringSlotRaw(keyA, {0}).text == QStringLiteral("in-A"),
              QStringLiteral("Raw: 键文本有 REF 别名时仍读键自己的槽（不重解析）"));
    }

    // =====================================================================
    qDebug() << "\n7) 负下标：读空 / 写 false，两侧都「没有发生」";
    {
        VariableStorage vs;
        vs.registerCharaDataVariable(QStringLiteral("XS"), true, 1);
        vs.addChara(10);

        const int localSize = vs.localStrSize();
        check(vs.getStringSlot(QStringLiteral("PLAIN"), {-1}).text.isEmpty(),
              QStringLiteral("读: 普通槽负下标返回空串"));
        check(vs.getStringSlot(QStringLiteral("LOCALS"), {-1}).text.isEmpty(),
              QStringLiteral("读: LOCALS 负下标返回空串"));
        check(vs.getStringSlot(QStringLiteral("ARGS"), {-1}).text.isEmpty(),
              QStringLiteral("读: ARGS 负下标返回空串"));
        check(vs.getStringSlot(QStringLiteral("XS"), {-1, 0}).text.isEmpty(),
              QStringLiteral("读: CHARADATA 负角色号返回空串"));
        check(!vs.setStringSlot(QStringLiteral("PLAIN"), {-1}, sv(QStringLiteral("x"))),
              QStringLiteral("写: 普通槽负下标返回 false"));
        check(!vs.setStringSlot(QStringLiteral("LOCALS"), {-1}, sv(QStringLiteral("x"))),
              QStringLiteral("写: LOCALS 负下标返回 false"));
        check(!vs.setStringSlot(QStringLiteral("XS"), {0, -1}, sv(QStringLiteral("x"))),
              QStringLiteral("写: CHARADATA 负元素下标返回 false"));
        check(vs.localStrSize() == localSize,
              QStringLiteral("写: 负下标没有触发 LOCALS 懒扩容（没有发生）"));
        check(vs.getStringSlot(QStringLiteral("LOCALS"), {0}).text.isEmpty()
                  && vs.getStringSlot(QStringLiteral("PLAIN"), {0}).text.isEmpty(),
              QStringLiteral("写: 负下标没有落到别的格子（没有发生）"));

        // 角色号解析失败（void chara -> 模板号 -1）：两侧同样「没有发生」。
        VariableStorage voids;
        voids.registerCharaDataVariable(QStringLiteral("XS"), true, 1);
        voids.addVoidChara();
        check(voids.getStringSlot(QStringLiteral("XS"), {0, 0}).text.isEmpty()
                  && !voids.setStringSlot(QStringLiteral("XS"), {0, 0}, sv(QStringLiteral("x"))),
              QStringLiteral("CHARADATA: 角色槽解析失败时读空 / 写 false（对称）"));
    }

    // =====================================================================
    qDebug() << "\n================================";
    qDebug().noquote() << "断言数:" << g_assertions;
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}