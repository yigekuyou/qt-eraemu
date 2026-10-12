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
// test_file_case.cpp
//
// 「加载文件必须走引擎自带的大小写还原」的回归。
//
// 背景：Windows 的文件系统大小写不敏感，AUTHOR 在 Windows 上跑起来的游戏，
// 脚本/头文件/_Rename/配置里写的路径大小写**不必**与磁盘一致；搬到 Linux 上就
// 变成 file not found。引擎自带的 `FileSystem::findActualDir` /
// `getPathWithActualCase`（扫目录、大小写不敏感地还原真实名字）此前是**死代码**
// （零调用），加载路径全都在直接 `TextCodecUtil::readFile(path)`。
//
// 本测试锁住修复：`FileSystem::resolvePathCase()` + 各加载入口（这里是
// ErbLoader::loadFile 作为代表）在**大小写不一致**的路径上必须能读到文件。
//
// 不依赖 Qt Test；失败返回非零退出码。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include "Content/erb_loader.h"
#include "eraengine.h"
#include "GameData/file_system_io.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) {
        qDebug().noquote() << "  [ok ]" << what;
    } else {
        qDebug().noquote() << "  [FAIL]" << what;
        ++g_failures;
    }
}

static bool writeFileAt(const QString& path, const QString& text) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(text.toUtf8());
    return true;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "FileSystem::resolvePathCase / ErbLoader 大小写回归";
    qDebug() << "=================================================";

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        qDebug() << "  [FAIL] 无法创建临时目录";
        return 1;
    }

    // 磁盘上真实的大小写：Sub/Foo.ERB
    const QString realDir = tmp.filePath(QStringLiteral("Sub"));
    const QString realFile = tmp.filePath(QStringLiteral("Sub/Foo.ERB"));
    check(QDir(tmp.path()).mkpath(QStringLiteral("Sub")), QStringLiteral("建目录 Sub/"));
    check(writeFileAt(realFile, QStringLiteral("PRINT hello\n")), QStringLiteral("写 Sub/Foo.ERB"));

    const QString wrongDir = tmp.filePath(QStringLiteral("sub"));
    const QString wrongFile = tmp.filePath(QStringLiteral("sub/foo.erb"));

    if (QFileInfo::exists(wrongFile)) {
        qDebug() << "  [skip] 该文件系统大小写不敏感（如 macOS 默认）——本用例只在区分大小写时有意义";
        return 0;
    }

    // ---- 1. 逐段还原：目录名与文件名都要还原 ----
    const QString fixedDir = FileSystem::resolvePathCase(wrongDir);
    check(fixedDir == realDir, QStringLiteral("resolvePathCase 还原目录名 sub -> Sub"));

    const QString fixedFile = FileSystem::resolvePathCase(wrongFile);
    check(fixedFile == realFile, QStringLiteral("resolvePathCase 还原整条路径 sub/foo.erb -> Sub/Foo.ERB"));
    check(QFileInfo::exists(fixedFile), QStringLiteral("还原后的路径确实存在"));

    // ---- 2. 快路径：已经正确时原样返回 ----
    check(FileSystem::resolvePathCase(realFile) == realFile,
          QStringLiteral("已存在的路径原样返回（快路径，不扫目录）"));

    // ---- 3. 不存在的路径：原样返回，交给调用方报「找不到」 ----
    const QString ghost = tmp.filePath(QStringLiteral("nope/Nothing.ERB"));
    check(FileSystem::resolvePathCase(ghost) == ghost,
          QStringLiteral("不存在的路径原样返回（不伪造）"));

    check(FileSystem::getPathWithActualCase(tmp.path(), QStringLiteral("sub/foo.erb")) == realFile,
          QStringLiteral("相对路径包含文件名时返回完整文件路径"));
    check(FileSystem::getPathWithActualCase(tmp.path(), QStringLiteral("sub/missing.erb"))
              == QDir(realDir).filePath(QStringLiteral("missing.erb")),
          QStringLiteral("缺失文件不返回父目录冒充文件"));
    check(writeFileAt(tmp.filePath(QStringLiteral("Sub/foo.erb")), QStringLiteral("PRINT other\n")),
          QStringLiteral("创建仅大小写不同的文件"));
    check(FileSystem::resolvePathCase(realFile) == realFile,
          QStringLiteral("同名异大小写文件存在时精确匹配优先"));

    // ---- 4. 加载入口真的用上了它 ----
    //   ErbLoader::readFileContent 先 resolvePathCase 再读；
    //   修复前这一步在 Linux 上必然读不到 -> loadFile 返回 false。
    ErbLoader loader;
    check(loader.loadFile(wrongFile), QStringLiteral("ErbLoader::loadFile 能用小写路径读到 Sub/Foo.ERB"));

    ErbLoader loader2;
    check(loader2.loadFile(wrongDir + QStringLiteral("/FOO.erb")),
          QStringLiteral("ErbLoader::loadFile 混大小写路径也读得到"));

    // 空路径不能被 QDir 当作工作目录；包装目录不探测其 data 子目录。
    const QString oldCwd = QDir::currentPath();
    check(QDir::setCurrent(tmp.path()), "设置含 ERB 文件的工作目录");
    ErbLoader invalid;
    check(!invalid.loadDirectory(QString()), "同步加载拒绝空路径");
    check(invalid.getLoadedScripts().isEmpty(), "空路径不扫描工作目录");
    int completed = 0;
    QObject::connect(&invalid, &ErbLoader::loadCompleted, [&](bool ok) {
        ++completed;
        check(!ok, "异步无效路径报告失败");
    });
    invalid.loadDirectoryAsync(QString());
    invalid.loadDirectoryAsync(tmp.filePath("missing"));
    check(completed == 2 && !invalid.isLoading(), "异步无效路径立即返回且未启动任务");
    check(invalid.getLoadedScripts().isEmpty(), "异步无效路径不扫描工作目录");

    check(QDir(tmp.path()).mkpath("wrapped/data/CSV"), "创建包装目录 CSV");
    check(QDir(tmp.path()).mkpath("wrapped/data/ERB"), "创建包装目录 ERB");
    check(writeFileAt(tmp.filePath("wrapped/data/ERB/MAIN.ERB"),
                      "@SYSTEM_TITLE\nPRINTL loaded\nINPUT\n"), "写入合法子目录脚本");
    const QString wrapper = tmp.filePath("wrapped");
    for (bool async : {false, true}) {
        EraEngine engine;
        int resultCount = 0;
        QObject::connect(&engine, &EraEngine::scriptsLoaded, [&](bool ok) {
            ++resultCount;
            check(!ok, "游戏路径错误发出 scriptsLoaded(false)");
        });
        if (async) engine.loadAsync(wrapper);
        else engine.setGameDirectory(wrapper);
        check(resultCount == 1 && engine.hasError(), "路径错误立即报告失败");
        check(engine.getGameDirectory() == wrapper, "不探测或重定向到 data");
        check(engine.getParseTable()->scriptNames().isEmpty(), "错误游戏路径未载入脚本");
        check(!engine.isLoadingScripts(), "错误路径未启动后台装载");
    }
    // 只有直接选择正确的目录才加载；缺少任一必需目录仍失败。
    EraEngine valid;
    valid.setGameDirectory(tmp.filePath("wrapped/data"));
    check(!valid.hasError() && valid.getParseTable()->scriptNames().size() == 1,
          "直接选择 CSV/ERB 所在目录正常装载");
    check(QDir(tmp.path()).mkpath("incomplete/CSV"), "创建缺 ERB 的目录");
    valid.setGameDirectory(tmp.filePath("incomplete"));
    check(valid.hasError() && valid.getParseTable()->scriptNames().isEmpty(),
          "缺 ERB 时失败并清除前一次解析表");
    check(QDir(tmp.path()).mkpath("no-csv/ERB"), "创建缺 CSV 的目录");
    valid.setGameDirectory(tmp.filePath("no-csv"));
    check(valid.hasError() && valid.getParseTable()->scriptNames().isEmpty(),
          "缺 CSV 时失败且不加载 ERB");
    check(QDir::setCurrent(oldCwd), "还原工作目录");

    qDebug() << (g_failures == 0 ? "\n全部通过" : "\n有失败");
    return g_failures == 0 ? 0 : 1;
}
