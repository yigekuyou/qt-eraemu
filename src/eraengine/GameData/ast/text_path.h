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
#ifndef AST_TEXT_PATH_H
#define AST_TEXT_PATH_H

#include <QChar>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// emuera.em 私family 的**路径规范化**（`Utils.GetValidPath`）与
// LOADTEXT / SAVETEXT 的扩展名白名单（`Config.ValidExtension`）。
//
// C# 原文（emuera.em/Emuera/Runtime/Utils/EvilMask/Utils.cs:214）：
//
//     public static string GetValidPath(string path) {
//         path = path.Replace('/', '\\').Replace("..\\", "");
//         try {
//             if (Path.GetPathRoot(path) != string.Empty) return null;
//         } catch { return null; }
//         return Path.Combine(Program.ExeDir + path);
//     }
//
// 使用者（全部同一语义）：LOADTEXT / SAVETEXT 的**路径形态**、EXISTFILE、
// ENUMFILES（Creator.Method.cs:1255 / 223 / 6898 / 7007）。
//
// 语义要点（逐条对齐，**不是**「安全化路径」那种正规化）：
//   · '/' 与 '\\' 都当分隔符（脚本两种写法都有）；
//   · 删除**所有** "..\\" 片段（字符串层面的 Replace，不是按目录段处理）：
//     "..\\..\\a" -> "a"，"a..\\b" -> "ab"（脚本侧副作用，见 test/data/functions/EXISTFILE.md）；
//   · 「有根」路径一律拒绝（Windows 的 "C:\\x" / "\\x" / UNC "\\\\srv\\share"、
//     POSIX 的 "/x"）—— 脚本不得读到游戏目录之外；
//   · 结果拼在**游戏目录**下（C# 的 Program.ExeDir；本移植的「可执行目录」
//     就是游戏目录，与 SAVETEXT/LOADTEXT 的落盘基准一致）。
//
// 与 C# 的唯一差别：C# 在 Windows 上按 '\\' 拼接，本移植跨平台，归一到 '/'
// 之后交给 QDir 拼接（QFile/QDir 在 Windows 上同样接受 '/'）。
// ---------------------------------------------------------------------------
namespace erapath {

// 归一（'\\' -> '/'）+ 删除所有 "../" 片段。与 C# 的
// `Replace('/', '\\').Replace("..\\", "")` 等价（分隔符换成 '/' 后比较）。
[[nodiscard]] inline QString normalize(const QString& path) {
    QString p = path;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // C# 是**无条件删除** "..\" 子串（含 "a..\b" -> "ab" 这种副作用），
    // 这里保持同一行为：只要出现 "../" 就删除，直到不再出现。
    const QString escape = QStringLiteral("../");
    int at = p.indexOf(escape);
    while (at >= 0) {
        p.remove(at, escape.size());
        at = p.indexOf(escape, at);
    }
    return p;
}

// 「有根」判定（对齐 `Path.GetPathRoot(path) != ""`）：POSIX 绝对路径、
// Windows 盘符（"C:" 之后无论跟不跟分隔符）、UNC（"//srv/share"）。
[[nodiscard]] inline bool isRooted(const QString& path) {
    if (path.isEmpty()) return false;
    if (path.startsWith(QLatin1Char('/'))) return true;                  // "/x"、"//srv/share"
    if (path.size() >= 2 && path.at(1) == QLatin1Char(':')
        && ((path.at(0) >= QLatin1Char('A') && path.at(0) <= QLatin1Char('Z'))
            || (path.at(0) >= QLatin1Char('a') && path.at(0) <= QLatin1Char('z'))))
        return true;                                                     // "C:" / "C:/x"
    return false;
}

// GetValidPath：baseDir 下的合法路径；**空串 = C# 的 null（拒绝）**。
// baseDir 为空（引擎尚未设置游戏目录）时按当前目录解析 —— 与 C# 的
// `Program.ExeDir + path`（ExeDir 必有值）保持同一形态，不回退成「拒绝」。
[[nodiscard]] inline QString resolveUnderBase(const QString& baseDir, const QString& path) {
    const QString p = normalize(path);
    if (isRooted(p)) return QString();
    if (baseDir.isEmpty()) return QDir::cleanPath(p);
    return QDir(baseDir).absoluteFilePath(p);
}

// `Path.GetExtension(filepath).ToLower().Substring(1)`：不带点的小写扩展名。
// 无扩展名（或点在**文件名**开头，如 ".gitignore"）时返回空串
// （C# Path.HasExtension 对 ".gitignore" 为 true、GetExtension 为 ".gitignore"
//  → Substring(1) = "gitignore"；这里按同一规则：点名在段首也算扩展名）。
[[nodiscard]] inline QString extensionOf(const QString& filepath) {
    const QString leaf = QFileInfo(filepath).fileName();
    const int dot = leaf.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot == leaf.size() - 1) return QString();
    return leaf.mid(dot + 1).toLower();
}

// 扩展名是否在 `Config.ValidExtension` 白名单内（C# `ValidExtension.Contains(tmp)`；
// 白名单项大小写不敏感地比较，配置里写 "TXT" 也能用）。
[[nodiscard]] inline bool extensionAllowed(const QStringList& validExtensions,
                                           const QString& filepath) {
    const QString ext = extensionOf(filepath);
    for (const QString& candidate : validExtensions) {
        if (QString::compare(candidate.trimmed(), ext, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

// 路径解析失败时的基准探针：baseDir 下的某个候选是否存在（EXISTFILE 用）。
[[nodiscard]] inline bool existsUnderBase(const QString& baseDir, const QString& path) {
    const QString resolved = resolveUnderBase(baseDir, path);
    if (resolved.isEmpty()) return false;
    return QFileInfo::exists(resolved) && !QFileInfo(resolved).isDir();
}

} // namespace erapath

#endif // AST_TEXT_PATH_H
