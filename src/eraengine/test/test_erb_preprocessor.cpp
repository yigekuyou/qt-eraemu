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
// test_erb_preprocessor.cpp
//
// 验证 ERB 行读入与预处理（对齐 C# Sub/EraStreamReader.ReadEnabledLine
// + GameProc/ErbLoader.PPState）：
//   1. '{' ... '}' 行连接（逻辑行取 '{' 所在物理行号，被吞并行留空）
//   2. [SKIPSTART]/[SKIPEND] 区域禁用
//   3. [IF]/[ELSEIF]/[ELSE]/[ENDIF] 宏开关（宏来自 .ERH 的 #DEFINE）
//   4. [IF_DEBUG]/[IF_NDEBUG]
//   5. _Rename.csv 替换（[[X]] -> 值）
//   6. 告警：'{' 未闭合 / [ENDIF] 不对应 / 括号后有多余内容
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QSet>

#include "erb_preprocessor.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

// 把处理结果拼成 "行号:文本" 便于断言
static QStringList dump(const QList<ErbSourceLine>& lines) {
    QStringList out;
    for (const ErbSourceLine& l : lines) {
        out << QStringLiteral("%1:%2").arg(l.physicalLine).arg(l.text);
    }
    return out;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "ErbPreprocessor test";
    qDebug() << "====================";

    qDebug() << "\n1) '{' ... '}' 行连接";
    {
        ErbPreprocessor pp;
        QStringList warns;
        const QString src =
            "@F(ARG)\n"
            "{\n"
            "#DIM A, 1, 2\n"
            "}\n"
            "#DIM B\n"
            "A = 1\n";
        const QList<ErbSourceLine> out = pp.process(src, &warns, "t.ERB");
        check(out.size() == 6, QString("物理行数 == 6（得到 %1）").arg(out.size()));
        check(out.size() > 1 && out[1].text == QStringLiteral("#DIM A, 1, 2"),
              QString("{ 行合并为 #DIM A, 1, 2（得到 %1）")
                  .arg(out.size() > 1 ? out[1].text : QString()));
        check(out.size() > 2 && out[2].text.isEmpty(), "被吞并行（#DIM A,1,2）为空");
        check(out.size() > 3 && out[3].text.isEmpty(), "'} ' 行本身为空");
        check(out.size() > 4 && out[4].text == QStringLiteral("#DIM B"), "合并结束后行号继续对齐");
        check(warns.isEmpty(), QString("无告警（得到 %1 条）").arg(warns.size()));
        check(dump(out).value(1).startsWith(QStringLiteral("2:")), "合并行取 '{' 所在的物理行号 2");
    }

    qDebug() << "\n2) [SKIPSTART] / [SKIPEND]";
    {
        ErbPreprocessor pp;
        QStringList warns;
        const QString src =
            "A = 1\n"
            "[SKIPSTART]\n"
            "CASE\n"
            "BROKEN EXPRESSION ###\n"
            "[SKIPEND]\n"
            "B = 2\n";
        const QList<ErbSourceLine> out = pp.process(src, &warns, "t.ERB");
        check(out.size() == 6, "行数保持（6）");
        check(out[0].text == QStringLiteral("A = 1"), "SKIPSTART 之前正常");
        check(out[2].text.isEmpty(), "SKIPSTART 区域内容被禁用");
        check(out[3].text.isEmpty(), "SKIPSTART 区域内容被禁用(2)");
        check(out[5].text == QStringLiteral("B = 2"), "SKIPEND 之后恢复");
    }

    qDebug() << "\n3) [IF 宏] / [ELSE] / [ENDIF]";
    {
        const QString src =
            "[IF DEBUG]\n"
            "PRINTDEBUG\n"
            "[ELSE]\n"
            "PRINTNORMAL\n"
            "[ENDIF]\n";
        {
            ErbPreprocessor pp;   // DEBUG 未定义
            QStringList warns;
            const QList<ErbSourceLine> out = pp.process(src, &warns, "t.ERB");
            check(out[1].text.isEmpty(), "未定义宏 -> 第一分支禁用");
            check(out[3].text == QStringLiteral("PRINTNORMAL"), "某分支启用");
        }
        {
            ErbPreprocessor pp;
            QStringList warns;
            pp.setMacros(QSet<QString>{QStringLiteral("DEBUG")});
            const QList<ErbSourceLine> out = pp.process(src, &warns, "t.ERB");
            check(out[1].text == QStringLiteral("PRINTDEBUG"), "定义了宏 -> 第一分支启用");
            check(out[3].text.isEmpty(), "ELSE 分支禁用");
        }
    }

    qDebug() << "\n4) [IF_DEBUG]";
    {
        const QString src = "[IF_DEBUG]\nA = 1\n[ELSE]\nB = 2\n[ENDIF]\n";
        ErbPreprocessor on;
        on.setDebugMode(true);
        QStringList w1;
        const QList<ErbSourceLine> r1 = on.process(src, &w1, "t.ERB");
        check(r1[1].text == QStringLiteral("A = 1"), "调试模式 -> IF_DEBUG 分支启用");

        ErbPreprocessor off;
        QStringList w2;
        const QList<ErbSourceLine> r2 = off.process(src, &w2, "t.ERB");
        check(r2[3].text == QStringLiteral("B = 2"), "非调试模式 -> ELSE 分支启用");
    }

    qDebug() << "\n5) _Rename.csv（[[X]] -> 值）";
    {
        const QString csv =
            ";comment\n"
            "0 , 你\n"
            "4 , 魅魔\n"
            "126 , 紫苑\n"
            "A\\,B , 带逗号\n"
            "noComma\n";
        const ErbPreprocessor::RenameMap map = ErbPreprocessor::parseRenameCsv(csv);
        check(map.value(QStringLiteral("[[你]]")) == QStringLiteral("0"), "[[你]] -> 0");
        check(map.value(QStringLiteral("[[魅魔]]")) == QStringLiteral("4"), "[[魅魔]] -> 4");
        check(map.value(QStringLiteral("[[带逗号]]")) == QStringLiteral("A,B"),
              QString("转义逗号：[[带逗号]] -> A,B（得到 %1）")
                  .arg(map.value(QStringLiteral("[[带逗号]]"))));
        check(!map.contains(QStringLiteral("[[noComma]]")), "无逗号的行被忽略");

        ErbPreprocessor pp;
        pp.setRenameMap(map);
        QStringList warns;
        const QList<ErbSourceLine> out =
            pp.process(QStringLiteral("FLAG:[[魅魔]] = [[你]]\n"), &warns, "t.ERB");
        check(out.value(0).text == QStringLiteral("FLAG:4 = 0"),
              QString("行内替换（得到 %1）").arg(out.value(0).text));
    }

    qDebug() << "\n6) #DEFINE 收集";
    {
        const QString erh =
            "#DEFINE 時間帯 TIME:2\n"
            "#DIM CONST X = 1\n"
            "#DEFINE GO_OUT_MAX_NO 10\n"
            ";#DEFINE IGNORED 1\n";
        const QSet<QString> macros = ErbPreprocessor::collectDefines(erh);
        check(macros.contains(QStringLiteral("時間帯")), "收集到 時間帯");
        check(macros.contains(QStringLiteral("GO_OUT_MAX_NO")), "收集到 GO_OUT_MAX_NO");
        check(!macros.contains(QStringLiteral("IGNORED")), "注释行不算");
        check(macros.size() == 2, QString("宏数 == 2（得到 %1）").arg(macros.size()));
    }

    qDebug() << "\n7) 告警";
    {
        ErbPreprocessor pp;
        QStringList warns;
        (void)pp.process(QStringLiteral("{\nA = 1\n"), &warns, "t.ERB");
        check(!warns.isEmpty() && warns.first().contains(QStringLiteral("缺少对应的 '}'")),
              "未闭合的 '{' 告警");

        warns.clear();
        (void)pp.process(QStringLiteral("[ENDIF]\n"), &warns, "t.ERB");
        check(!warns.isEmpty(), "[ENDIF] 不对应时告警");

        warns.clear();
        (void)pp.process(QStringLiteral("[SKIPSTART extra]\n"), &warns, "t.ERB");
        check(!warns.isEmpty(), "[SKIPSTART] 多余参数时告警");
    }

    qDebug() << "\n====================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] preprocessor tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
