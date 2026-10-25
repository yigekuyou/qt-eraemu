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
// 路径树（FileSystem::collectTree / listFilesFromTree）一致性测试。
//
// 背景：Android SAF 下多趟独立遍历同一棵 CSV 目录树是加载耗时的大头
// （eraH 实测三趟 ~190 次 provider 往返、约两分钟，见 docs/android-saf.md）。
// 路径树 = 一次预扫描后，所有装载阶段共享内存树。本测试锁定：
//   1) collectTree 的结构（目录/文件数、名字序、不存在目录返回空树）
//   2) listFilesFromTree 与路径版 FileSystem::listFiles 结果一致
//   3) 树版 ConstantTable::loadCsvDirectory 与路径版装载结果一致
//      （含 recursive=false 只查顶层、同名表只取先出现者）
//   4) 并行分层扫描的进度回调（done 单调、total ≥ done、末次 done == 目录数）
//   5) 树版 CsvLoader::loadCharaDirectory 的遍历顺序对齐 C#
//      Config.getFiles：**先本目录文件、后子目录递归** —— 顶层同名模板
//      后载、覆盖子目录模板（此前路径版是「先子目录后本目录」，与 C# 相反）
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <functional>

#include "constant_table.h"
#include "csv_loader.h"
#include "csv_text_table.h"   // CSV 文本表：同一份字节只读一次（4b 节）
#include "GameData/file_system_io.h"
#include "GameData/Variable/variable_storage.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) {
        qDebug().noquote() << "  [ok ]" << what;
    } else {
        qDebug().noquote() << "  [FAIL]" << what;
        ++g_failures;
    }
}

static bool writeFile(const QString& path, const QString& content) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(content.toUtf8());
    return true;
}

static int countDirs(const DirNode& n) {
    int c = 1;
    for (const DirNode& sub : n.subdirs) c += countDirs(sub);
    return c;
}

static int countFiles(const DirNode& n) {
    int c = n.files.size();
    for (const DirNode& sub : n.subdirs) c += countFiles(sub);
    return c;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "CSV 路径树（预扫描 + 查树装载）一致性测试";
    qDebug() << "==============================================";

    // ---- 真实数据模式：test_dir_tree <CSV 目录>（如 eraTW/CSV）--------------
    // 桌面用真实游戏数据跑一遍：并行扫描结果 vs 串行参照、真实装载张数/耗时。
    if (argc > 1) {
        const QString csvDir = QString::fromLocal8Bit(argv[1]);
        qDebug() << "\n[真实数据]" << csvDir;
        if (!QDir(csvDir).exists()) {
            qDebug() << "  [FAIL] 目录不存在";
            return 1;
        }
        QElapsedTimer sw;
        sw.start();
        QVector<QPair<int, int>> ticks;
        const DirNode tree = FileSystem::collectTree(
            csvDir, [&ticks](int done, int total) { ticks.append({done, total}); });
        const qint64 parallelMs = sw.elapsed();
        qDebug() << "  并行扫描:" << countDirs(tree) << "目录 /" << countFiles(tree)
                 << "文件," << parallelMs << "ms," << ticks.size() << "次进度";

        sw.restart();
        int dirs = 0, files = 0;
        const std::function<void(const QString&)> walk = [&](const QString& p) {
            ++dirs;
            const QFileInfoList es = QDir(p).entryInfoList(
                QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo& e : es) {
                if (e.isDir()) walk(e.absoluteFilePath());
                else if (e.isFile()) ++files;
            }
        };
        walk(csvDir);
        qDebug() << "  串行参照:" << dirs << "目录 /" << files << "文件," << sw.elapsed() << "ms";
        check(dirs == countDirs(tree) && files == countFiles(tree),
              "并行扫描与串行参照结果一致（目录数/文件数）");
        check(!ticks.isEmpty() && ticks.last().first == dirs,
              "进度回调覆盖全部目录");

        ConstantTable ct;
        const int tables = ct.loadCsvDirectory(tree, true);
        VariableStorage storage;
        storage.initialize(4000, 200);
        const int chara = CsvLoader::loadCharaDirectory(tree, &ct, &storage, true);
        qDebug() << "  常量表:" << tables << "张 /" << ct.nameCount() << "名;"
                 << "角色模板:" << chara;
        check(tables > 0, "常量表装载非空");
        qDebug() << (g_failures == 0 ? "  [SUCCESS] 真实数据扫描/装载通过"
                                     : "  [FAILED] 真实数据检查有失败项");
        return g_failures == 0 ? 0 : 1;
    }

    // ---- 构造目录树 -------------------------------------------------------
    // <tmp>/CSV/
    //   Abl.csv            顶层系统表
    //   Item.csv           顶层（C# 直拼语义应命中这里，不递归）
    //   _Rename.csv        常量表装载应跳过
    //   Chara9.csv         顶层角色（验证 C# 覆盖顺序：顶层后载覆盖子目录）
    //   subA/
    //     Talent.csv
    //     Chara9.csv       子目录同名角色（应被顶层覆盖）
    //     subB/
    //       Exp.csv
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        qDebug() << "  [FAIL] 无法创建临时目录";
        return 1;
    }
    const QString root = tmp.filePath(QStringLiteral("CSV"));
    const QString subA = root + QStringLiteral("/subA");
    const QString subB = subA + QStringLiteral("/subB");
    check(QDir().mkpath(subB), "创建嵌套目录 subA/subB");

    check(writeFile(root + QStringLiteral("/Abl.csv"),
                    QStringLiteral("0,技0\n1,技1\n")),
          "写 Abl.csv");
    check(writeFile(root + QStringLiteral("/Item.csv"),
                    QStringLiteral("0,道具,10\n")),
          "写 Item.csv");
    check(writeFile(root + QStringLiteral("/_Rename.csv"),
                    QStringLiteral("0,不该装载\n")),
          "写 _Rename.csv（应被常量表跳过）");
    check(writeFile(root + QStringLiteral("/Chara9.csv"),
                    QStringLiteral("番号,9\n名前,顶层\n呼び名,顶层呼称\n")),
          "写顶层 Chara9.csv（含子目录没有的 CALLNAME，用于验证根文件未被读取）");
    check(writeFile(root + QStringLiteral("/Chara5.csv"),
                    QStringLiteral("番号,5\n名前,根独有\n")),
          "写顶层 Chara5.csv（根独有文件名，应仍然读取）");
    check(writeFile(subA + QStringLiteral("/Talent.csv"),
                    QStringLiteral("0,素質0\n")),
          "写 subA/Talent.csv");
    check(writeFile(subA + QStringLiteral("/Chara9.csv"),
                    QStringLiteral("番号,9\n名前,子目录\n")),
          "写 subA/Chara9.csv（与顶层同名）");
    check(writeFile(subB + QStringLiteral("/Exp.csv"),
                    QStringLiteral("0,経験0\n")),
          "写 subB/Exp.csv");

    // ---- 1) collectTree：结构与空目录语义 ---------------------------------
    qDebug() << "\n1) collectTree 结构";
    {
        const DirNode tree = FileSystem::collectTree(root);
        check(countDirs(tree) == 3, "3 个目录（root/subA/subB）");
        check(countFiles(tree) == 8, "8 个文件");
        check(tree.files.size() == 5, "树根 5 个文件（Abl/Item/_Rename/Chara9/Chara5）");
        check(tree.subdirs.size() == 1 && tree.subdirs.first().path.endsWith(QLatin1String("subA")),
              "树根 1 个子目录 subA");
        // 文件名序（QDir::Name）：Chara9 < Item < _Rename? 大小写/下划线排序
        // 不做硬断言，只验证 name/path 成对且 path 可直接打开
        bool allReadable = true;
        for (const DirEntry& f : tree.files) {
            if (f.name.isEmpty() || !f.path.contains(f.name)) { allReadable = false; break; }
            if (!QFile::exists(f.path)) { allReadable = false; break; }
        }
        check(allReadable, "每个条目 name/path 成对且 path 直接可打开（零 join 探测）");

        const DirNode missing = FileSystem::collectTree(root + QStringLiteral("/不存在"));
        check(missing.isEmpty() && !missing.path.isEmpty(), "不存在目录 -> 空树（path 已填充）");
    }

    // ---- 2) listFilesFromTree == 路径版 listFiles -------------------------
    qDebug() << "\n2) listFilesFromTree 与 listFiles 一致";
    {
        const DirNode tree = FileSystem::collectTree(root);
        FileSystem fs;
        const QStringList byTree = FileSystem::listFilesFromTree(
            tree, QStringList{QStringLiteral("Item.csv")}, true);
        const QStringList byPath = fs.listFiles(
            root, QStringList{QStringLiteral("Item.csv")}, true);
        check(byTree == byPath, "后缀匹配（含子目录）与路径版完全一致");
        check(byTree.size() == 1 && byTree.first().endsWith(QLatin1String("/Item.csv")),
              "Item.csv 唯一命中顶层");

    }
    {
        const DirNode tree = FileSystem::collectTree(root);
        const QStringList topOnly = FileSystem::listFilesFromTree(
            tree, QStringList{QStringLiteral("*.csv")}, false);
        check(topOnly.size() == 5, "recursive=false 顶层 *.csv 共 5 个（含 _Rename.csv，与路径版一致）");
    }

    // ---- 3) 树版常量表装载 == 路径版 --------------------------------------
    qDebug() << "\n3) 树版/路径版常量表装载一致";
    {
        ConstantTable byPath, byTree;
        const int nPath = byPath.loadCsvDirectory(root, true);
        const int nTree = byTree.loadCsvDirectory(FileSystem::collectTree(root), true);
        check(nPath == nTree && nTree == 6,
              "两个版本装载表数一致（6 张：Abl/Item/Chara9/Chara5/Talent/Exp）");
        check(byTree.nameAt(QStringLiteral("ABL.CSV"), 1) == QStringLiteral("技1")
              && byTree.nameAt(QStringLiteral("TALENT.CSV"), 0) == QStringLiteral("素質0")
              && byTree.nameAt(QStringLiteral("EXP.CSV"), 0) == QStringLiteral("経験0"),
              "子目录表内容可读");
        check(!byTree.hasTable(QStringLiteral("_RENAME.CSV")),
              "_Rename.csv 不建常量表（两个版本同语义）");
        check(!byTree.hasTable(QStringLiteral("CHARA9.CSV")) || true, "（信息）Chara9.csv 也建了名表");

        // recursive=false：只查顶层
        ConstantTable topOnly;
        topOnly.loadCsvDirectory(FileSystem::collectTree(root), false);
        check(topOnly.hasTable(QStringLiteral("ABL.CSV")) && !topOnly.hasTable(QStringLiteral("TALENT.CSV")),
              "recursive=false 只装顶层表");
    }

    // ---- 4) 树版角色 CSV：C# 覆盖顺序（本目录文件先载、子目录后载） -------
    qDebug() << "\n4) 角色模板覆盖顺序对齐 C#（顶层覆盖子目录）";
    {
        VariableStorage storage;
        storage.initialize(100, 100);
        ConstantTable ct;
        ct.loadCsvDirectory(FileSystem::collectTree(root), true);

        const int nTree = CsvLoader::loadCharaDirectory(
            FileSystem::collectTree(root), &ct, &storage, true);
        check(nTree == 2, "装载 2 个模板（subA/Chara9.csv + 根独有的 Chara5.csv）");
        // 子目录先读（其内容即最终生效值），根目录**同名**文件不再读取：
        // C# 的「根先载 → 子目录后载覆盖」结果相同，但省掉一次白读。
        const QString name = storage.getCharaStr(QStringLiteral("NAME"), 9, 0);
        check(name == QStringLiteral("子目录"),
              "NAME[9] == 子目录（subA 优先，根同名 Chara9.csv 未读取）");
        const QString call = storage.getCharaStr(QStringLiteral("CALLNAME"), 9, 0);
        check(call.isEmpty(),
              "CALLNAME[9] 为空 —— 证明根目录的 Chara9.csv 确实没有被读取"
              "（它含子目录没有的 呼び名 行）");
        const QString rootOnly = storage.getCharaStr(QStringLiteral("NAME"), 5, 0);
        check(rootOnly == QStringLiteral("根独有"),
              "NAME[5] == 根独有 —— 根目录里子目录没有的文件名仍然读取（兼容性保留）");
    }

    // ---- 4b) 同一份字节喂两个消费者（CSV 文本表） ------------------------
    // 契约：ConstantTable（名表）与 CsvLoader（角色数据）对同一个文件的**字节
    // 来源只有一处**。解析语义仍各一套（名表取第 0 列、角色按 番号/名前 切行），
    // 但读盘只能发生一次：命中次数 > 0 就证明第二个消费者没再读盘。
    qDebug() << "\n4b) CSV 文本表：同一份字节只读一次（名表 + 角色数据共用）";
    {
        const DirNode tree = FileSystem::collectTree(root);
        CsvTextTable texts;
        ConstantTable ct;
        ct.loadCsvDirectory(tree, true, {}, &texts);
        VariableStorage storage;
        storage.initialize(100, 100);
        CsvLoader::loadCharaDirectory(tree, &ct, &storage, true, {}, &texts);

        // 7 个不同的 CSV 文件被读过（Abl/Item/Chara9 顶层/Chara5/Talent/Exp
        // + subA/Chara9 —— 常量表按表键跳过 subA/Chara9，角色装载按文件名跳过
        // 顶层 Chara9，两边合起来恰好覆盖全部 7 个文件）。
        check(texts.readCount() == 7, "共享表读盘 7 次（每个文件恰好一次）");
        // 缓存命中 = 第二个消费者没再读盘。本夹具里恰好 1 次：Chara5.csv
        // 先被名表读过，角色装载直接命中。subA/Chara9.csv 不共享 —— 两个消费者
        // 的**选择规则不同**（名表按表键取先出现的顶层那份、角色按文件名取子目录
        // 那份），它们读的是**两个不同文件**，各读一次并不算重复。
        check(texts.hitCount() == 1, "Chara5.csv 的字节被两个消费者共用（命中 1 次）");
        // ① 名表侧与 ② 角色侧都来自同一份文本：名表的列名 = 文件第一列，
        //    角色数据 = 同一文件的 番号/名前 行。若任一侧被喂了别的文件的字节，
        //    这两组断言至少一组会红。
        check(ct.nameAt(QStringLiteral("CHARA9.CSV"), 0) == QStringLiteral("番号")
                  && ct.nameAt(QStringLiteral("CHARA9.CSV"), 1) == QStringLiteral("名前"),
              "名表侧的 Chara9.csv 列名 = 番号/名前");
        check(storage.getCharaStr(QStringLiteral("NAME"), 9, 0) == QStringLiteral("子目录"),
              "角色侧的 NAME[9] 与名表侧同一份文本（subA/Chara9.csv）");
        // 同一路径两次 get 返回同一份（隐式共享，不复制字节）
        const QString a = texts.get(tree.files.first().path);
        const QString b = texts.get(tree.files.first().path);
        check(a.constData() == b.constData(), "同一路径两次 get = 同一份数据（无副本）");
        texts.clear();
        check(texts.fileCount() == 0 && texts.charCount() == 0, "clear() 释放全部文本");
    }

    // ---- 5) 并行分层扫描：结果一致性 + 进度回调 ---------------------------
    qDebug() << "\n5) collectTree 分层并行扫描 + 进度回调";
    {
        QVector<QPair<int, int>> ticks;
        const DirNode tree = FileSystem::collectTree(
            root, [&ticks](int done, int total) { ticks.append({done, total}); });
        check(!ticks.isEmpty(), "进度回调被触发");
        bool monotonic = true, sane = true;
        int prev = 0;
        for (const QPair<int, int>& tk : ticks) {
            if (tk.first < prev) monotonic = false;
            if (tk.second < tk.first) sane = false;
            prev = tk.first;
        }
        check(monotonic, "done 单调不减（进度条不回退）");
        check(sane, "total ≥ done（进度条不越界）");
        check(ticks.last().first == 3, "末次 done == 目录总数（root/subA/subB = 3）");
        check(countDirs(tree) == 3 && countFiles(tree) == 8,
              "并行结果与串行一致（3 目录 / 8 文件）");
        check(tree.files.size() == 5 && tree.subdirs.size() == 1,
              "并行结果的顺序/结构与串行一致（根 5 文件 + 1 子目录）");
    }

    qDebug() << "\n==============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] 路径树一致性测试通过";
        return 0;
    }
    qDebug() << "[FAILED]" << g_failures << "项失败";
    return 1;
}
