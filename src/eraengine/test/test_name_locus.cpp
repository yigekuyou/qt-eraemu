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
// test_name_locus.cpp —— 「名字 + 下标 -> 整数存储」的解析顺序护栏
//
// 契约（只走公开 API，与解析实现形态无关）：
//   1. 参数值绑定（hasParameter）优先：读不看 1D 槽，写只改参数；
//   2. CHARADATA 遮蔽同名 1D：按角色分块，与 1D 槽是两处存储；
//   3. CHARADATA 字符串的整型视图：读恒 0、写 no-op（无副作用）；
//   4. ARG / LOCAL 先于同名 1D 槽；
//   5. ARGS / LOCALS / RESULTS 读写不对称：整型写 no-op，整型读落 1D 槽；
//   6. 系统变量（FLAG）先于同名 1D 槽；
//   7. 维数判定：2D / 3D 走声明维数；1D 变量给多下标时只取第 0 个；
//   8. 私有名 + 显式 scope 与广域同名量互不干扰；
//   9. 名字同时是 CHARADATA 与系统变量时，CHARADATA 行先命中（今天的两行重叠）。
//
// 这一组先在旧实现（readIntByName / writeIntByName 各一份 if 链）上全绿；
// 把解析收敛成单份 resolveIntLocus 之后断言条数与结果必须完全一致。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
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

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    VariableStorage vs;
    // 声明的维数（等价于 #DIM Z, 2,3 / #DIM Z3, 2,2,2 / #DIM W, 4）：
    // intDimension 的 provider 是 Old ExecutionEngine::lhsDimension 的同源注入点。
    vs.setIntDimProvider([](const QString& name) -> int {
        const QString upper = eraUpperKey(name);
        if (upper == QLatin1String("Z")) return 2;
        if (upper == QLatin1String("Z3")) return 3;
        return 1;
    });
    // 多维几何（装载期 installMultiDimArray 的等价登记）：行主序 strides。
    vs.installMultiDimArray(QStringLiteral("Z"), {2, 3}, {3, 1}, false, true);
    vs.installMultiDimArray(QStringLiteral("Z3"), {2, 2, 2}, {4, 2, 1}, false, true);

    // 角色数据变量（EraParseTable 登记录入的等价形式）。
    vs.registerCharaDataVariable(QStringLiteral("X"), false, 1);
    vs.registerCharaDataVariable(QStringLiteral("XS"), true, 1);

    const eravar::ScopeId global = eravar::kGlobalScope;

    // =====================================================================
    qDebug() << "\n1) 参数值绑定优先";
    {
        vs.setParameter(QStringLiteral("P"), 7);
        vs.setGlobalInt1D(QStringLiteral("P"), 0, 9);
        check(vs.readIntByName(QStringLiteral("P"), {0}, global) == 7,
              QStringLiteral("P: readIntByName 读到值绑定 7（不看 1D 槽的 9）"));
        check(vs.getGlobalInt1D(QStringLiteral("P"), 0) == 9,
              QStringLiteral("P: 同名 1D 槽仍是 9（读参数不碰槽）"));
        vs.writeIntByName(QStringLiteral("P"), {0}, 5, global);
        check(vs.readIntByName(QStringLiteral("P"), {0}, global) == 5,
              QStringLiteral("P: writeIntByName 写进值绑定（读回 5）"));
        check(vs.parameter(QStringLiteral("P")).toLongLong() == 5,
              QStringLiteral("P: parameter() 就是被写的值绑定"));
        check(vs.getGlobalInt1D(QStringLiteral("P"), 0) == 9,
              QStringLiteral("P: 写参数不改 1D 槽（仍是 9）"));
    }

    // =====================================================================
    qDebug() << "\n2) CHARADATA 遮蔽同名 1D（按角色分块）";
    {
        vs.writeIntByName(QStringLiteral("X"), {0, 0}, 11, global);
        vs.writeIntByName(QStringLiteral("X"), {1, 0}, 22, global);
        check(vs.readIntByName(QStringLiteral("X"), {0, 0}, global) == 11,
              QStringLiteral("X: 角色 0 元素 0 == 11"));
        check(vs.readIntByName(QStringLiteral("X"), {1, 0}, global) == 22,
              QStringLiteral("X: 角色 1 元素 0 == 22（两角色互不干扰）"));
        vs.setGlobalInt1D(QStringLiteral("X"), 0, 33);
        check(vs.readIntByName(QStringLiteral("X"), {0, 0}, global) == 11,
              QStringLiteral("X: 同名 1D 槽写 33 后角色 0 仍为 11"));
        check(vs.readIntByName(QStringLiteral("X"), {1, 0}, global) == 22,
              QStringLiteral("X: 同名 1D 槽写 33 后角色 1 仍为 22"));
        check(vs.getGlobalInt1D(QStringLiteral("X"), 0) == 33,
              QStringLiteral("X: 角色写入不落到同名 1D 槽（槽仍是 33）"));
        check(vs.getCharaInt(QStringLiteral("X"), 0, 0) == 11,
              QStringLiteral("X: 公开 getCharaInt 与按名读同源（11）"));
    }

    // =====================================================================
    qDebug() << "\n3) CHARADATA 字符串的整型视图（读 0 / 写 no-op）";
    {
        vs.writeIntByName(QStringLiteral("XS"), {0, 0}, 77, global);
        check(vs.readIntByName(QStringLiteral("XS"), {0, 0}, global) == 0,
              QStringLiteral("XS: 字符串角色变量整型读恒 0"));
        check(vs.getStringSlot(QStringLiteral("XS"), {0}).text.isEmpty(),
              QStringLiteral("XS: 整型写没有产生全局字符串副作用"));
        check(vs.getCharaStr(QStringLiteral("XS"), 0, 0).isEmpty(),
              QStringLiteral("XS: 整型写没有碰角色字符串槽"));
        check(vs.getGlobalInt1D(QStringLiteral("XS"), 0) == 0,
              QStringLiteral("XS: 整型写没有落到同名 1D 槽"));
    }

    // =====================================================================
    qDebug() << "\n4) ARG / LOCAL 先于同名 1D";
    {
        vs.writeIntByName(QStringLiteral("ARG"), {3}, 5, global);
        check(vs.getArgInt(3) == 5, QStringLiteral("ARG:3 写进实参槽（5）"));
        check(vs.getGlobalInt1D(QStringLiteral("ARG"), 3) == 0,
              QStringLiteral("ARG: 同名 1D 槽仍是 0"));
        vs.setGlobalInt1D(QStringLiteral("ARG"), 3, 41);
        check(vs.readIntByName(QStringLiteral("ARG"), {3}, global) == 5,
              QStringLiteral("ARG: 读先走实参槽（5，不是 1D 槽的 41）"));
        check(vs.getGlobalInt1D(QStringLiteral("ARG"), 3) == 41,
              QStringLiteral("ARG: 1D 槽本身保留 41"));

        vs.writeIntByName(QStringLiteral("LOCAL"), {2}, 6, global);
        check(vs.getLocalInt(2) == 6, QStringLiteral("LOCAL:2 写进局部槽（6）"));
        check(vs.getGlobalInt1D(QStringLiteral("LOCAL"), 2) == 0,
              QStringLiteral("LOCAL: 同名 1D 槽仍是 0"));
        vs.setGlobalInt1D(QStringLiteral("LOCAL"), 2, 42);
        check(vs.readIntByName(QStringLiteral("LOCAL"), {2}, global) == 6,
              QStringLiteral("LOCAL: 读先走局部槽（6，不是 1D 槽的 42）"));
    }

    // =====================================================================
    qDebug() << "\n5) ARGS / LOCALS / RESULTS：写 no-op，读落 1D 槽（今天的不对称）";
    {
        check(vs.readIntByName(QStringLiteral("ARGS"), {2}, global) == 0,
              QStringLiteral("ARGS:2 初始为 0"));
        vs.writeIntByName(QStringLiteral("ARGS"), {2}, 5, global);
        check(vs.readIntByName(QStringLiteral("ARGS"), {2}, global) == 0,
              QStringLiteral("ARGS: 整型写是 no-op（读仍 0）"));
        check(vs.getArgStr(2).isEmpty(),
              QStringLiteral("ARGS: 整型写没有碰字符串槽"));
        vs.setGlobalInt1D(QStringLiteral("ARGS"), 2, 7);
        check(vs.readIntByName(QStringLiteral("ARGS"), {2}, global) == 7,
              QStringLiteral("ARGS: 读确实落到 1D 槽（读到 7）"));

        vs.writeIntByName(QStringLiteral("LOCALS"), {1}, 5, global);
        check(vs.readIntByName(QStringLiteral("LOCALS"), {1}, global) == 0,
              QStringLiteral("LOCALS: 整型写是 no-op（读仍 0）"));
        vs.setGlobalInt1D(QStringLiteral("LOCALS"), 1, 8);
        check(vs.readIntByName(QStringLiteral("LOCALS"), {1}, global) == 8,
              QStringLiteral("LOCALS: 读确实落到 1D 槽（读到 8）"));

        vs.writeIntByName(QStringLiteral("RESULTS"), {1}, 5, global);
        check(vs.readIntByName(QStringLiteral("RESULTS"), {1}, global) == 0,
              QStringLiteral("RESULTS: 整型写是 no-op（读仍 0）"));
        vs.setGlobalInt1D(QStringLiteral("RESULTS"), 1, 9);
        check(vs.readIntByName(QStringLiteral("RESULTS"), {1}, global) == 9,
              QStringLiteral("RESULTS: 读确实落到 1D 槽（读到 9）"));
    }

    // =====================================================================
    qDebug() << "\n6) 系统变量先于同名 1D";
    {
        vs.writeIntByName(QStringLiteral("FLAG"), {1}, 5, global);
        check(vs.getFlag(1) == 5, QStringLiteral("FLAG:1 写进系统槽（5）"));
        check(vs.getGlobalInt1D(QStringLiteral("FLAG"), 1) == 0,
              QStringLiteral("FLAG: 同名 1D 槽仍是 0"));
        vs.setGlobalInt1D(QStringLiteral("FLAG"), 1, 99);
        check(vs.readIntByName(QStringLiteral("FLAG"), {1}, global) == 5,
              QStringLiteral("FLAG: 读先走系统槽（5，不是 1D 槽的 99）"));
        check(vs.getGlobalInt1D(QStringLiteral("FLAG"), 1) == 99,
              QStringLiteral("FLAG: 1D 槽本身保留 99"));
    }

    // =====================================================================
    qDebug() << "\n7) 维数判定的三条边";
    {
        vs.writeIntByName(QStringLiteral("Z"), {1, 2}, 501, global);
        check(vs.readIntByName(QStringLiteral("Z"), {1, 2}, global) == 501,
              QStringLiteral("Z:[1,2] 走 2D 槽并读回 501"));
        check(vs.getGlobalInt2D(QStringLiteral("Z"), 1, 2) == 501,
              QStringLiteral("Z: getGlobalInt2D 落到同一格"));
        check(vs.getGlobalInt1D(QStringLiteral("Z"), 1) == 0,
              QStringLiteral("Z: 2D 写入不污染扁平槽 1"));

        vs.writeIntByName(QStringLiteral("Z3"), {1, 1, 1}, 601, global);
        check(vs.readIntByName(QStringLiteral("Z3"), {1, 1, 1}, global) == 601,
              QStringLiteral("Z3:[1,1,1] 走 3D 槽并读回 601"));
        check(vs.getGlobalInt3D(QStringLiteral("Z3"), 1, 1, 1) == 601,
              QStringLiteral("Z3: getGlobalInt3D 落到同一格"));

        // 1D 声明 + 两个下标：只取第 0 个，第二个被忽略（旧 writeLhs 的判定）。
        vs.writeIntByName(QStringLiteral("W"), {1, 5}, 701, global);
        check(vs.getGlobalInt1D(QStringLiteral("W"), 1) == 701,
              QStringLiteral("W: 两个下标只取第 0 个（1D 槽 1 == 701）"));
        check(vs.readIntByName(QStringLiteral("W"), {1, 5}, global) == 701,
              QStringLiteral("W: 同形读回 701"));
        check(vs.readIntByName(QStringLiteral("W"), {1}, global) == 701,
              QStringLiteral("W: 单下标读到同一格（701）"));
        check(vs.getGlobalInt1D(QStringLiteral("W"), 5) == 0,
              QStringLiteral("W: 第二下标 5 没有参与取址（槽 5 仍 0）"));
    }

    // =====================================================================
    qDebug() << "\n8) 私有名 + 显式 scope 与广域同名量隔离";
    {
        const eravar::ScopeId sid = eravar::internName(QStringLiteral("NAMELOCUS_PRIV"));
        vs.setPrivateScope(sid, {QStringLiteral("W")});
        vs.writeIntByName(QStringLiteral("W"), {0}, 801, sid);
        check(vs.readIntByName(QStringLiteral("W"), {0}, sid) == 801,
              QStringLiteral("W@私有: 显式 scope 写入并读回 801"));
        vs.setPrivateScope(global, {});
        vs.setGlobalInt1D(QStringLiteral("W"), 0, 802);
        check(vs.readIntByName(QStringLiteral("W"), {0}, global) == 802,
              QStringLiteral("W@广域: 同名量读到 802"));
        check(vs.readIntByName(QStringLiteral("W"), {0}, sid) == 801,
              QStringLiteral("W@私有: 广域写入不干扰私有量（仍 801）"));
        check(vs.getGlobalInt1D(QStringLiteral("W"), 0) == 802,
              QStringLiteral("W@广域: 1D 槽本身是 802"));
    }

    // =====================================================================
    qDebug() << "\n9) CHARADATA 行先于系统变量行（两行都命中时）";
    {
        // TFLAG 同时出现在系统存储名单（sysvar::systemStorageNames）里，又被
        // 登记成 CHARADATA —— 只有这种重叠名才能钉住「两行谁先命中」。
        vs.registerCharaDataVariable(QStringLiteral("TFLAG"), false, 1);
        vs.writeIntByName(QStringLiteral("TFLAG"), {0, 0}, 55, global);
        check(vs.getCharaInt(QStringLiteral("TFLAG"), 0, 0) == 55,
              QStringLiteral("TFLAG/CHARADATA: 写进角色槽（55）"));
        check(vs.getTflag(0) == 0,
              QStringLiteral("TFLAG/系统: 系统槽未被写（0）"));
        vs.setTflag(0, 66);
        check(vs.readIntByName(QStringLiteral("TFLAG"), {0, 0}, global) == 55,
              QStringLiteral("TFLAG: 读先命中 CHARADATA 行（55，不是系统槽的 66）"));
        check(vs.getTflag(0) == 66,
              QStringLiteral("TFLAG: 系统槽本身保留 66"));
    }

    // =====================================================================
    qDebug() << "\n================================";
    qDebug().noquote() << "断言数:" << g_assertions;
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}