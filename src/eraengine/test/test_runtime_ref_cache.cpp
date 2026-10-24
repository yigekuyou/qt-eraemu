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
// GETVAR / GETVARS / SETVAR 的「名字文本 -> 变量项」缓存：**契约测试**
//
// 被锁住的东西（每一条都是缓存可能引入的新失效面，与实现无关）：
//   1. 同一文本连续执行 N 次：结果相同，且第 2..N 次都命中（缓存真的生效）。
//   2. 同名但不同段（不同 scopeKey）：互不串（私有变量按段分离）。
//   3. 文本相同、段相同、**脚本被替换**之后：必须拿到新结果（失效点③/④）。
//   4. prepare 对合法输入是 no-op（markupTemplate 恒空）—— 所以缓存不需要
//      为「打印模板」单列一个键维度；而 ignoreTripleSymbols 变了必须整表作废。
//   5. 容量上限 512：满了淘汰最久未用，淘汰后重新解析的结果不变。
//   6. 缓存的是 **AST**，不是值：命中后仍读存储（SETVAR 写完 GETVAR 立刻变）。
//
// 全部走 ExtensionRegistry 的**真实调用路径**（与引擎装配同形），不直接调用
// 内部函数 —— 缓存若改变了 GETVAR 的可观察行为，这里必红。
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QStringList>
#include <stdexcept>

#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "process_state.h"
#include "variable_storage.h"

static int checks = 0;
static int failures = 0;
static void check(bool ok, const QString& what) {
    ++checks;
    if (ok) {
        qInfo().noquote() << "PASS" << what;
    } else {
        ++failures;
        qCritical().noquote() << "FAIL" << what;
    }
}

// 与 EraEngine 装配期相同的接线：解析表提供「名字 -> AST / 属性」，求值器提供
// 「写回」，ExecutionEngine 自己提供 evaluateNode。
struct Rig {
    VariableStorage storage;
    ProcessState state;
    EraParseTable table;
    ExecutionEngine engine;
    ExpressionEvaluator evaluator;

    Rig()
        : table(&state), engine(&storage, nullptr) {
        engine.setParseTable(&table);
        engine.setExpressionEvaluator(&evaluator);
        table.setExpressionEvaluator(&evaluator);
        table.setVariableStorage(&storage);
        evaluator.setExtensionFunctionInvoker(
            [this](const QString& name, const QList<QVariant>& args,
                   const QList<const ExpressionNode*>& nodes, QVariant& out) {
                return engine.extensions().runExpression(name, args, nodes, out);
            });
        engine.extensions().setVariableNameServices(
            [this](const QString& text) { return table.runtimeVariableReference(text); },
            [this](const QString& name) { return table.variableFlags(name); },
            [this](const ExpressionNode& target, const QVariant& value) {
                return evaluator.assignNodeValue(target, &storage, nullptr, value);
            });
    }

    void load(const QString& script, const QStringList& source, bool header = false) {
        QList<LogicalLine> lines;
        lines.reserve(source.size());
        for (int i = 0; i < source.size(); ++i) {
            lines.append(AstBuilder::build(source.at(i), ScriptPosition(script, i, 0),
                [this](const QString& text) { return table.expressionAst(text); }));
        }
        table.loadScript(script, lines, header);
    }

    // 真实调用路径：`GETVAR("...")`。失败时把错误文本放进 *err。
    QVariant get(const QString& fn, const QString& text, QString* err = nullptr) {
        QVariant out;
        try {
            if (!engine.extensions().runExpression(fn, {QVariant(text)}, {}, out)) return QVariant();
        } catch (const std::exception& e) {
            if (err) *err = QString::fromUtf8(e.what());
            return QVariant();
        }
        return out;
    }
    qint64 getvar(const QString& text, QString* err = nullptr) {
        return get(QStringLiteral("GETVAR"), text, err).toLongLong();
    }
    QString getvars(const QString& text, QString* err = nullptr) {
        return get(QStringLiteral("GETVARS"), text, err).toString();
    }
    bool setvar(const QString& text, const QVariant& value) {
        QVariant out;
        try {
            return engine.extensions().runExpression(QStringLiteral("SETVAR"),
                                                     {QVariant(text), value}, {}, out);
        } catch (const std::exception&) {
            return false;
        }
    }
};

// 行号按 raw 文本找（不手写魔术下标）。
static int lineOf(const EraParseTable& table, const QString& script, const QString& prefix) {
    const ScriptData* sd = table.script(script);
    if (!sd) return -1;
    for (int i = 0; i < sd->lines.size(); ++i) {
        if (sd->lines.at(i).raw.trimmed().startsWith(prefix)) return i;
    }
    return -1;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    // ---- 1) 同一文本连续 N 次：结果相同 + 命中 N-1 -------------------------
    {
        Rig rig;
        // TESTARR 声明在头文件里 = 广域：测试可以直接写存储，不必先跑脚本。
        rig.load(QStringLiteral("g.ERH"), {QStringLiteral("#DIM TESTARR, 4")}, /*header=*/true);
        rig.load(QStringLiteral("cache.ERB"), {
            QStringLiteral("@MAIN"),
            QStringLiteral("RETURN"),
        });
        rig.table.markLoadComplete();
        const int mainLine = lineOf(rig.table, QStringLiteral("cache.ERB"), QStringLiteral("@MAIN"));
        check(mainLine >= 0, QStringLiteral("1.0 找到 @MAIN 段"));
        rig.table.setPosition(QStringLiteral("cache.ERB"), mainLine, false);
        rig.storage.setGlobalInt1D(QStringLiteral("TESTARR"), 1, 7, eravar::kGlobalScope);
        check(rig.storage.getGlobalInt1D(QStringLiteral("TESTARR"), 1, eravar::kGlobalScope) == 7,
              QStringLiteral("1.0b 存储写入生效（否则本节是空测）"));

        const quint64 hits0 = rig.table.runtimeRefCacheHits();
        const quint64 miss0 = rig.table.runtimeRefCacheMisses();
        bool allSame = true;
        for (int i = 0; i < 10; ++i) allSame &= rig.getvar(QStringLiteral("TESTARR:1")) == 7;
        check(allSame, QStringLiteral("1.1 同一 GETVAR(\"TESTARR:1\") 连续 10 次都是 7"));
        check(rig.table.runtimeRefCacheHits() - hits0 == 9
                  && rig.table.runtimeRefCacheMisses() - miss0 == 1,
              QStringLiteral("1.2 10 次执行 = 1 miss + 9 hit（缓存真的生效）"));
        check(rig.table.runtimeRefCacheSize() == 1,
              QStringLiteral("1.3 只解析出一条缓存条目（按 (文本, 段) 去重）"));
        // 空白差异 = 同一个输入（解析入口自己也只 trim）→ 不应产生第二条。
        check(rig.getvar(QStringLiteral("  TESTARR:1 ")) == 7 && rig.table.runtimeRefCacheSize() == 1,
              QStringLiteral("1.4 前后空白不制造第二条缓存"));
        // 失败解析（空文本 -> 空 AST）不写缓存，也不影响既有条目。
        QString err;
        rig.getvar(QStringLiteral(""), &err);
        check(err.contains(QStringLiteral("不是变量")) && rig.table.runtimeRefCacheSize() == 1,
              QStringLiteral("1.5 空文本报「不是变量」且不写缓存"));
        check(rig.getvar(QStringLiteral("1+2"), &err) == 0
                  && err.contains(QStringLiteral("不是变量")),
              QStringLiteral("1.6 非变量表达式「1+2」报「不是变量」"));
    }

    // ---- 2) 同名不同段：互不串 ---------------------------------------------
    {
        Rig rig;
        rig.load(QStringLiteral("scope.ERB"), {
            QStringLiteral("@MAIN"),
            QStringLiteral("RETURN"),          // 1
            QStringLiteral("@SEG_A"),          // 2
            QStringLiteral("#DIM PRIVX"),      // 3
            QStringLiteral("COUNT:0 = 0"),     // 4
            QStringLiteral("RETURN"),          // 5
            QStringLiteral("@SEG_B"),          // 6
            QStringLiteral("#DIM PRIVX"),      // 7
            QStringLiteral("COUNT:0 = 0"),     // 8
            QStringLiteral("RETURN"),          // 9
        });
        rig.table.markLoadComplete();
        const int a = lineOf(rig.table, QStringLiteral("scope.ERB"), QStringLiteral("@SEG_A")) + 2;
        const int b = lineOf(rig.table, QStringLiteral("scope.ERB"), QStringLiteral("@SEG_B")) + 2;
        check(a > 0 && b > 0, QStringLiteral("2.0 找到两段的语句行"));

        rig.table.setPosition(QStringLiteral("scope.ERB"), a, false);
        check(rig.setvar(QStringLiteral("PRIVX"), qint64(5)), QStringLiteral("2.1 SETVAR 写 A 段 PRIVX"));
        rig.table.setPosition(QStringLiteral("scope.ERB"), b, false);
        check(rig.setvar(QStringLiteral("PRIVX"), qint64(9)), QStringLiteral("2.2 SETVAR 写 B 段 PRIVX"));

        bool ok = true;
        for (int i = 0; i < 5; ++i) {           // 交替读，强制两种段都进出缓存
            rig.table.setPosition(QStringLiteral("scope.ERB"), a, false);
            ok &= rig.getvar(QStringLiteral("PRIVX")) == 5;
            rig.table.setPosition(QStringLiteral("scope.ERB"), b, false);
            ok &= rig.getvar(QStringLiteral("PRIVX")) == 9;
        }
        check(ok, QStringLiteral("2.3 同名 PRIVX 在两段交替读 5 轮互不串（5/9）"));
        check(rig.table.runtimeRefCacheSize() == 2,
              QStringLiteral("2.4 同名不同段 = 两条缓存条目"));
        // 段身份必须真的进了键：直接看 AST 的访问键。
        rig.table.setPosition(QStringLiteral("scope.ERB"), a, false);
        const auto astA = rig.table.runtimeVariableReference(QStringLiteral("PRIVX"));
        rig.table.setPosition(QStringLiteral("scope.ERB"), b, false);
        const auto astB = rig.table.runtimeVariableReference(QStringLiteral("PRIVX"));
        const auto* va = astA ? static_cast<const VariableNode*>(astA.data()) : nullptr;
        const auto* vb = astB ? static_cast<const VariableNode*>(astB.data()) : nullptr;
        check(va && vb && va->accessKey.debugKeyText() != vb->accessKey.debugKeyText(),
              QStringLiteral("2.5 两段的 AST 访问键不同（段身份确实在键里）"));
    }

    // ---- 3) 脚本替换后必须失效（最容易漏的一条）----------------------------
    // 用一个**能被 GETVAR 看见**的差异：`#DIM CONST` 的加入会改变下标的解析
    // （CONSTIDX 从「未声明变量 -> 0」变成「常量 -> 1」），于是同一个文本
    // "ARR:CONSTIDX" 必须读到另一个元素。AST 若被旧缓存复用，答案就是旧的。
    {
        Rig rig;
        rig.load(QStringLiteral("g.ERH"), {QStringLiteral("#DIM ARR, 4")}, /*header=*/true);
        rig.load(QStringLiteral("main.ERB"), {
            QStringLiteral("@MAIN"),
            QStringLiteral("RETURN"),
        });
        rig.load(QStringLiteral("late.ERH"), {QStringLiteral("; 还没有 CONSTIDX")}, /*header=*/true);
        rig.table.markLoadComplete();
        const int mainLine = lineOf(rig.table, QStringLiteral("main.ERB"), QStringLiteral("@MAIN"));
        rig.table.setPosition(QStringLiteral("main.ERB"), mainLine, false);
        rig.storage.setGlobalInt1DInCurrentFrame(QStringLiteral("ARR"), 0, 10);
        rig.storage.setGlobalInt1DInCurrentFrame(QStringLiteral("ARR"), 1, 20);

        check(rig.getvar(QStringLiteral("ARR:CONSTIDX")) == 10,
              QStringLiteral("3.1 装载①：CONSTIDX 未声明 -> 下标 0 -> 10"));
        check(rig.table.runtimeRefCacheSize() == 1, QStringLiteral("3.2 条目已进缓存"));
        const quint64 missBefore = rig.table.runtimeRefCacheMisses();

        // 增量装载：late.ERH 加上 `#DIM CONST CONSTIDX = 1`。
        check(rig.table.beginIncrementalLoad(), QStringLiteral("3.3 beginIncrementalLoad"));
        rig.load(QStringLiteral("late.ERH"),
                 {QStringLiteral("#DIM CONST CONSTIDX = 1")}, /*header=*/true);
        rig.table.markLoadComplete();
        rig.table.setPosition(QStringLiteral("main.ERB"), mainLine, false);

        check(rig.getvar(QStringLiteral("ARR:CONSTIDX")) == 20,
              QStringLiteral("3.4 装载②：同一文本必须拿到新结论（下标 1 -> 20）"));
        check(rig.table.runtimeRefCacheMisses() > missBefore,
              QStringLiteral("3.5 重新解析（旧缓存条目没有复活）"));
        check(rig.getvar(QStringLiteral("CONSTIDX")) == 1,
              QStringLiteral("3.6 新常量本身也认得（GETVAR(\"CONSTIDX\") = 1）"));
    }

    // ---- 4) print 模板：prepare 对合法输入是 no-op；开关变了整表作废 --------
    {
        Rig rig;
        rig.load(QStringLiteral("tpl.ERB"), {
            QStringLiteral("@MAIN"),
            QStringLiteral("#DIMS S"),
            QStringLiteral("S '= \"x\""),
            QStringLiteral("RETURN"),
        });
        rig.table.markLoadComplete();
        const int line = lineOf(rig.table, QStringLiteral("tpl.ERB"), QStringLiteral("S '="));
        rig.table.setPosition(QStringLiteral("tpl.ERB"), line, false);
        const auto ast = rig.table.runtimeVariableReference(QStringLiteral("S"));
        check(ast && ast->kind() == NodeKind::Variable,
              QStringLiteral("4.1 \"S\" 解析为单个变量项"));
        check(ast && ast->markupTemplate.isNull(),
              QStringLiteral("4.2 合法输入的 prepare 是 no-op（markupTemplate 空）"));
        const auto again = rig.table.runtimeVariableReference(QStringLiteral("S"));
        check(again.data() == ast.data(),
              QStringLiteral("4.3 第二次拿到的是**同一个** AST（共享，不深拷贝）"));

        // ignoreTripleSymbols 是 StrForm **解析**的输入，但它只在**世代边界**
        // 快照（每次调用去问 provider 实测 ~10 µs，见 syncRuntimeRefCache 的注释；
        // 而它对 GETVAR 族合法输入没有可观察影响：StrForm 文本一律「不是变量」）。
        // 于是两条断言一起锁住这个取舍：同一世代内不查 provider；下一次装载重读。
        bool triple = false;
        rig.evaluator.setFormIgnoreProvider([&triple] { return triple; });
        (void)rig.table.runtimeVariableReference(QStringLiteral("S"));
        const quint64 missBefore = rig.table.runtimeRefCacheMisses();
        const quint64 hitsBefore = rig.table.runtimeRefCacheHits();
        triple = true;
        (void)rig.table.runtimeVariableReference(QStringLiteral("S"));
        check(rig.table.runtimeRefCacheHits() == hitsBefore + 1
                  && rig.table.runtimeRefCacheMisses() == missBefore,
              QStringLiteral("4.4 同一世代内不逐次查 provider（命中，不付 10 µs）"));
        // 世代边界（增量装载）重新快照 provider -> 整表作废。
        check(rig.table.beginIncrementalLoad(), QStringLiteral("4.5 进入下一装载世代"));
        rig.load(QStringLiteral("tpl.ERB"), {
            QStringLiteral("@MAIN"),
            QStringLiteral("#DIMS S"),
            QStringLiteral("S '= \"x\""),
            QStringLiteral("RETURN"),
        });
        rig.table.markLoadComplete();
        rig.table.setPosition(QStringLiteral("tpl.ERB"), line, false);
        const quint64 missAtLoad = rig.table.runtimeRefCacheMisses();
        (void)rig.table.runtimeVariableReference(QStringLiteral("S"));
        check(rig.table.runtimeRefCacheMisses() == missAtLoad + 1,
              QStringLiteral("4.6 装载世代边界重新快照并作废（同文本重新解析）"));
    }

    // ---- 5) 容量上限与淘汰 -------------------------------------------------
    {
        Rig rig;
        // CAPARR:0 是**有值的广域数组元素**：它的答案（111）与洪水里的
        // 未声明名（恒 0）不同 —— 旧条目被别的 AST 顶替时这里立刻读成 0。
        rig.load(QStringLiteral("cap.ERH"),
                 {QStringLiteral("#DIM CAPARR, 2")}, /*header=*/true);
        rig.load(QStringLiteral("cap.ERB"), {
            QStringLiteral("@MAIN"),
            QStringLiteral("RETURN"),
        });
        rig.table.markLoadComplete();
        const int line = lineOf(rig.table, QStringLiteral("cap.ERB"), QStringLiteral("@MAIN"));
        rig.table.setPosition(QStringLiteral("cap.ERB"), line, false);
        rig.storage.setGlobalInt1D(QStringLiteral("CAPARR"), 0, 111, eravar::kGlobalScope);

        const int cap = EraParseTable::kRuntimeRefCacheCapacity;
        check(cap == 512, QStringLiteral("5.0 容量上限 = 512"));
        check(rig.getvar(QStringLiteral("CAPARR:0")) == 111,
              QStringLiteral("5.0b 先建立一条「有值」的条目（=111）"));

        for (int i = 0; i < cap + 100; ++i) {
            rig.getvar(QStringLiteral("GEN_%1:0").arg(i));
        }
        check(rig.table.runtimeRefCacheSize() == cap,
              QStringLiteral("5.1 溢出后条目数保持在上限内（%1）").arg(cap));
        // 最近用过的仍在：命中（未声明 -> 0）。
        const quint64 hitsBefore = rig.table.runtimeRefCacheHits();
        rig.getvar(QStringLiteral("GEN_%1:0").arg(cap + 99));
        check(rig.table.runtimeRefCacheHits() == hitsBefore + 1,
              QStringLiteral("5.2 最近插入的条目命中（LRU 不误伤新条目）"));
        // 最早的已被淘汰：重新解析（miss），但结果必须仍然正确。
        const quint64 missBefore = rig.table.runtimeRefCacheMisses();
        check(rig.getvar(QStringLiteral("GEN_0:0")) == 0
                  && rig.table.runtimeRefCacheMisses() == missBefore + 1,
              QStringLiteral("5.3 被淘汰的条目重新解析，结果正确"));
        // 淘汰后同一个键必须拿到**它自己的**解析结果（不能被别的名字的 AST 顶替）。
        check(rig.getvar(QStringLiteral("CAPARR:0")) == 111,
              QStringLiteral("5.4 被淘汰的键重新解析出自己的值（111，不是别人的 0）"));
    }

    // ---- 6) 缓存的是 AST 不是值 --------------------------------------------
    {
        Rig rig;
        rig.load(QStringLiteral("val.ERB"), {
            QStringLiteral("@MAIN"),
            QStringLiteral("#DIM V, 2"),
            QStringLiteral("V:0 = 0"),
            QStringLiteral("RETURN"),
        });
        rig.table.markLoadComplete();
        const int line = lineOf(rig.table, QStringLiteral("val.ERB"), QStringLiteral("V:0 ="));
        rig.table.setPosition(QStringLiteral("val.ERB"), line, false);
        check(rig.getvar(QStringLiteral("V:0")) == 0, QStringLiteral("6.1 初值 0"));
        check(rig.setvar(QStringLiteral("V:0"), qint64(42)), QStringLiteral("6.2 SETVAR 写 42"));
        check(rig.getvar(QStringLiteral("V:0")) == 42,
              QStringLiteral("6.3 缓存命中后仍从存储读值（42）"));
        check(rig.setvar(QStringLiteral("V:1"), qint64(13)), QStringLiteral("6.4 SETVAR 写 V:1 = 13"));
        check(rig.getvar(QStringLiteral("V:1")) == 13,
              QStringLiteral("6.5 另一下标（另一条文本）也走真实存储"));
    }

    qInfo().noquote() << QStringLiteral("---- %1 条断言，%2 条失败").arg(checks).arg(failures);
    return failures == 0 ? 0 : 1;
}
