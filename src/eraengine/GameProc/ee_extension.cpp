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
// ee_extension.cpp —— EE 扩展实现
//
// 全部经 ExtensionRegistry 的简单函数（reg / services()）——实现不直接
// 依赖引擎：变量写入走 services().storage，存档目录走 services().savDirectory
// （惰性 provider，setGameDirectory 之后才可知）。
//
// 本轮真实现：EE 存档系（对齐 C# Creator.Method.cs）：
//   · CHKVARDATA "<文件名>"     CheckdataStrMethod(EraSaveFileType.Var)
//   · CHKGLOBALDATA <编号>      CheckdataMethod(EraSaveFileType.Global)
//   · FIND_VARDATA ["<通配符>"] FindFilesMethod(EraSaveFileType.Var)
// 其余 EE 扩展命令（test/data/emuera_ee_cmds.txt）保持「留痕跳过」桩。
// ---------------------------------------------------------------------------

#include <QDir>
#include <QFile>
#include <QStringList>

#include "ee_extension.h"
#include "extension_registry.h"
#include "variable_storage.h"

namespace {

// 对齐 C# EraDataState：0=OK / 1=FILENOTFOUND；RESULTS = 状态说明文本
constexpr int kDataOk = 0;
constexpr int kDataFileNotFound = 1;

// ---- 存档探测（注册类的服务桥接；实现不直接依赖引擎）-----------------------

// CHKVARDATA "<文件名>"（对齐 C# CheckdataStrMethod: Var 型、字符串文件名版）：
//   RESULT = EraDataState；RESULTS = 状态说明。
bool chkVarData(const LogicalLine& line, const QList<Operand>& args,
                const ExtensionRegistry& ext) {
    const ExtensionRegistry::Services& sv = ext.services();
    const QString file = args.isEmpty() ? QString() : args.first().raw.trimmed();
    if (file.isEmpty()) return false;   // 参数不足：交给通用路径留痕

    const QString dir = sv.savDirectory ? sv.savDirectory() : QString();
    const QString path = dir.isEmpty() ? file : dir + QLatin1Char('/') + file;
    const bool exists = QFile::exists(path);
    if (sv.storage) {
        sv.storage->setSystemVariable(QStringLiteral("RESULT"), 0,
                                      exists ? kDataOk : kDataFileNotFound);
        sv.storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0,
            exists ? QStringLiteral("ＯＫ") : QStringLiteral("ファイルが存在しません"));
    }
    qDebug() << "[ee-ext] CHKVARDATA" << path << (exists ? "存在" : "不存在")
             << "行" << line.position.toString();
    return true;
}

// CHKGLOBALDATA <编号>（对齐 C# CheckdataMethod: Global 型、编号版）：
//   全局存档文件（save_global.dat）存在性 -> RESULT / RESULTS。
bool chkGlobalData(const LogicalLine& line, const QList<Operand>& args,
                   const ExtensionRegistry& ext) {
    Q_UNUSED(line);
    Q_UNUSED(args);   // C# Global 型只有一个全局存档文件；编号不参与探测
    const ExtensionRegistry::Services& sv = ext.services();
    const QString dir = sv.savDirectory ? sv.savDirectory() : QString();
    const bool exists = !dir.isEmpty() && QFile::exists(dir + QStringLiteral("/save_global.dat"));
    if (sv.storage) {
        sv.storage->setSystemVariable(QStringLiteral("RESULT"), 0,
                                      exists ? kDataOk : kDataFileNotFound);
        sv.storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0,
            exists ? QStringLiteral("ＯＫ") : QStringLiteral("ファイルが存在しません"));
    }
    qDebug() << "[ee-ext] CHKGLOBALDATA" << (exists ? "存在" : "不存在");
    return true;
}

// FIND_VARDATA ["<通配符>"]（对齐 C# FindFilesMethod: Var 型文件探索）：
//   命中的文件路径逐个写入 RESULTS:0,1,2…（本移植数组动态增长，无需截断），
//   段数写 RESULT:0。
bool findVarData(const LogicalLine& line, const QList<Operand>& args,
                 const ExtensionRegistry& ext) {
    Q_UNUSED(line);
    const ExtensionRegistry::Services& sv = ext.services();
    const QString dir = sv.savDirectory ? sv.savDirectory() : QString();
    if (dir.isEmpty()) return true;
    const QString pattern = args.isEmpty() ? QStringLiteral("*")
                                           : args.first().raw.trimmed();
    const QFileInfoList files = QDir(dir).entryInfoList(
        QStringList{ pattern }, QDir::Files, QDir::Name);
    if (sv.storage) {
        for (int i = 0; i < files.size(); ++i)
            sv.storage->setGlobalStr1D(QStringLiteral("RESULTS"), i, files.at(i).fileName());
        sv.storage->setSystemVariable(QStringLiteral("RESULT"), 0, files.size());
    }
    qDebug() << "[ee-ext] FIND_VARDATA" << pattern << "->" << files.size() << "个文件";
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// EE 扩展登记（注册类构造时一次调用；默认全启用）
// ---------------------------------------------------------------------------
void registerEeExtensions(ExtensionRegistry& ext)
{
    // ---- EE 存档系：真实现（reg 带实现的重载；经注册类 services() 取用）----
    // 实现是三参函数（line, args, ext）——经 lambda 绑定注册类实例
    // （注册类与 lambda 同生命周期：lambda 存在注册类自己的表里）。
    ext.reg(QStringLiteral("CHKVARDATA"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return chkVarData(l, a, ext); });
    ext.reg(QStringLiteral("CHKGLOBALDATA"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return chkGlobalData(l, a, ext); });
    ext.reg(QStringLiteral("FIND_VARDATA"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return findVarData(l, a, ext); });

    // ---- EE 扩展命令（源: test/data/emuera_ee_cmds.txt，C# 权威源码导出）----
    // 只登记名字 -> 注册类统一「留痕一次 + 跳过」桩（待补全，不报错）。
    static const QStringList kEeCommands = {
        QStringLiteral("BINPUT"),
        QStringLiteral("BINPUTS"),
        QStringLiteral("CLEARMEMORY"),
        QStringLiteral("COLUMNBGCOLOR"),
        QStringLiteral("COLUMNCLEAR"),
        QStringLiteral("COLUMNCOLOR"),
        QStringLiteral("COLUMNCREATE"),
        QStringLiteral("COLUMNDIRECTION"),
        QStringLiteral("COLUMNMOVE"),
        QStringLiteral("COLUMNPRINT"),
        QStringLiteral("COLUMNPRINTL"),
        QStringLiteral("COLUMNPRINTW"),
        QStringLiteral("COLUMNRESIZE"),
        QStringLiteral("COLUMNWAIT"),
        QStringLiteral("EXISTFUNCTION"),
        QStringLiteral("EXISTSOUND"),
        QStringLiteral("FLOWINPUT"),
        QStringLiteral("FORCE_BEGIN"),
        QStringLiteral("FORCE_QUIT"),
        QStringLiteral("FORCE_QUIT_AND_RESTART"),
        QStringLiteral("FSTRJOIN"),
        QStringLiteral("FTOOLTIP_SETDURATION"),
        QStringLiteral("GDASHSTYLE"),
        QStringLiteral("GDRAWGWITHROTATE"),
        QStringLiteral("GDRAWLINE"),
        QStringLiteral("GDRAWTEXT"),
        QStringLiteral("GETDISPLAYLINE"),
        QStringLiteral("GETDOINGFUNCTION"),
        QStringLiteral("GETMEMORYUSAGE"),
        QStringLiteral("GETTEXTBOX"),
        QStringLiteral("GETTEXTSIZE"),
        QStringLiteral("GGETFONT"),
        QStringLiteral("GGETFONTSIZE"),
        QStringLiteral("GGETPEN"),
        QStringLiteral("GGETPENWIDTH"),
        QStringLiteral("GGETTEXTSIZE"),
        QStringLiteral("INPUTANY"),
        QStringLiteral("LCSVISASSI"),
        QStringLiteral("OCLEARLINE"),
        QStringLiteral("PLAYBGM"),
        QStringLiteral("PLAYSOUND"),
        QStringLiteral("QUIT_AND_RESTART"),
        QStringLiteral("SETBGMVOLUME"),
        QStringLiteral("SETSOUNDVOLUME"),
        QStringLiteral("SETTEXTBOX"),
        QStringLiteral("SKIPLOG"),
        QStringLiteral("SPRITEDISPOSEALL"),
        QStringLiteral("STOPBGM"),
        QStringLiteral("STOPSOUND"),
        QStringLiteral("STRJOIN1"),
        QStringLiteral("TINPUTAWAIT"),
        QStringLiteral("TOOLTIP_EXTENSION"),
        QStringLiteral("TOOLTIP_IMG"),
        QStringLiteral("TRYCALLF"),
        QStringLiteral("TRYCALLFORMF"),
        QStringLiteral("UPDATECHECK"),
    };
    for (const QString& name : kEeCommands) {
        ext.reg(name);
    }
}
