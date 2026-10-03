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
// test_train_reset.cpp —— 训练流程的变量重置
//   对齐 C# GameData/Variable/VariableEvaluator.cs：
//     * UpdateAfterShowUsercom(): UP/DOWN/LOSEBASE=0 + 各角色 DOWNBASE/CUP/CDOWN=0
//       （在 @SHOW_USERCOM 结束、以及 DOTRAIN 强制训练前调用）
//     * UpdateAfterInputCom():   各角色 NOWEX=0
//       （在 @EVENTCOM 之前调用 —— 与 ShowUsercom 的时机刻意不同）
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>

#include "variable_storage.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) {
        qDebug().noquote() << "  [ok ]" << what;
    } else {
        qDebug().noquote() << "  [FAIL]" << what;
        ++g_failures;
    }
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDebug() << "训练流程变量重置测试（C# UpdateAfterShowUsercom / UpdateAfterInputCom）";
    qDebug() << "==============================================================";

    VariableStorage st;
    st.initialize(10, 100);

    // ---- 预置脏值 ----
    st.setUp(0, 5);
    st.setUp(1, 6);
    st.setDown(0, 7);
    st.setLosebase(0, 8);
    st.setCharaInt(QStringLiteral("DOWNBASE"), 1, 0, 9);
    st.setCharaInt(QStringLiteral("CUP"), 1, 0, 10);
    st.setCharaInt(QStringLiteral("CDOWN"), 1, 0, 11);
    st.setCharaInt(QStringLiteral("NOWEX"), 1, 0, 12);

    qDebug() << "\n1) UpdateAfterShowUsercom()";
    st.updateAfterShowUsercom();
    check(st.getUp(0) == 0 && st.getUp(1) == 0, "UP 归零（全部下标）");
    check(st.getDown(0) == 0, "DOWN 归零");
    check(st.getLosebase(0) == 0, "LOSEBASE 归零");
    check(st.getCharaInt(QStringLiteral("DOWNBASE"), 1, 0) == 0, "角色 DOWNBASE 归零");
    check(st.getCharaInt(QStringLiteral("CUP"), 1, 0) == 0, "角色 CUP 归零");
    check(st.getCharaInt(QStringLiteral("CDOWN"), 1, 0) == 0, "角色 CDOWN 归零");
    // NOWEX 的时机不同：ShowUsercom 阶段**不**重置
    check(st.getCharaInt(QStringLiteral("NOWEX"), 1, 0) == 12,
          "NOWEX 不被 ShowUsercom 重置（时机在 EVENTCOM 前）");

    qDebug() << "\n2) UpdateAfterInputCom()";
    st.updateAfterInputCom();
    check(st.getCharaInt(QStringLiteral("NOWEX"), 1, 0) == 0, "角色 NOWEX 归零");

    qDebug() << "\n==============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] 训练流程变量重置测试通过";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
