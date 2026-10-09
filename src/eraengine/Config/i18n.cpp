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
#include "i18n.h"

#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include <mutex>

namespace eraengine {
namespace {

// 本体译文的候选目录：资源内的 :/i18n 优先，其次是可执行文件旁。
QStringList appTranslationDirs() {
    QStringList dirs;
    dirs << QStringLiteral(":/i18n");
    if (QCoreApplication::instance()) {
        const QString exe = QCoreApplication::applicationDirPath();
        dirs << exe + QStringLiteral("/translations");
        dirs << exe;
    }
    return dirs;
}

// 逐个目录尝试加载 emuera_<lang>.qm；.qm 的命名（含区域回退，如 zh_CN -> zh）
// 交给 QTranslator::load(QLocale, ...) 处理。
bool loadAppTranslator(QTranslator& tr, const QLocale& locale) {
    const QStringList dirs = appTranslationDirs();
    for (const QString& dir : dirs) {
        if (dir.isEmpty()) continue;
        if (tr.load(locale, QStringLiteral("emuera"), QStringLiteral("_"), dir, QStringLiteral(".qm"))) {
            return true;
        }
    }
    return false;
}

}  // namespace

int installTranslators() {
    static std::once_flag once;
    static int installedCount = 0;
    std::call_once(once, [] {
        QCoreApplication* app = QCoreApplication::instance();
        if (!app) return;  // 连 QCoreApplication 都没有：translate() 也会退化为源文本

        QString lang = qEnvironmentVariable("EMUERA_LANG");
        if (lang.isEmpty()) lang = QLocale::system().name();
        if (lang.isEmpty()) return;
        const QLocale locale(lang);

        // 1) Qt 自带控件/标准对话框的译文（qtbase_<lang>.qm），随 Qt 一起安装。
        auto* qtTr = new QTranslator(app);
        if (qtTr->load(QStringLiteral("qtbase_") + lang,
                       QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            app->installTranslator(qtTr);
            ++installedCount;
        } else {
            delete qtTr;
        }

        // 2) 本体译文（emuera_<lang>.qm），由本仓库的 lupdate/lrelease 生成。
        auto* selfTr = new QTranslator(app);
        if (loadAppTranslator(*selfTr, locale)) {
            app->installTranslator(selfTr);
            ++installedCount;
        } else {
            delete selfTr;
        }
    });
    return installedCount;
}

}  // namespace eraengine
