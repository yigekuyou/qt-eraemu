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
#ifndef ERA_ERB_AST_DISK_CACHE_H
#define ERA_ERB_AST_DISK_CACHE_H

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QList>
#include <QSaveFile>
#include <QString>
#include <QStringList>

struct ParsedErbFile;

// ---------------------------------------------------------------------------
// ERB AST 磁盘缓存（对标 Qt Quick 的 QML Disk Cache）
//
// 装载分两段：**解析**（读文件 → 解码 → 预处理 → 词法/语法 → LogicalLine AST）
// 与**语义**（finalizeParse：类型回填 / 函数重绑 / 参数校验）。解析段在 eraTW 上
// 约 4~5s，且完全由「源文件 + 编码 + 重命名表 + 常量表」决定 —— 与 QML 的
// qmlcachegen 产物一样，可以缓存到磁盘：第二次装载直接读回 ParsedErbFile，
// 跳过读取/解码/预处理/词法/语法。
//
// 语义段（finalizeParse）**不缓存**：它在后台线程执行（见 EraEngine），
// 依赖 VariableTable / 用户函数表等运行期状态，且每次都必须重跑。
//
// 正确性 / 失效
// ------------
//   · 格式版本 kFormatVersion（任何影响解析结果的代码改动都应 +1）；
//   · 目录内全部 .erb/.erh 的「路径 + 大小 + mtime」；
//   · 本地编码 / 调试模式；
//   · ERB 目录**兄弟层**里全部 .csv / .config 的戳（_Rename.csv、常量表、
//     编码配置都会影响 AST）—— 任一变化即整体失效。
// 任一不符 → load 返回 false，调用方回落正常解析（**透明，绝不产出错误结果**）。
//
// 缓存文件按 Qt QDataStream 布局；read 端对每一步都做边界检查，任何异常 /
// 截断都视为未命中（next() 返回 false），绝不把半截数据塞进解析表。
// ---------------------------------------------------------------------------
namespace ErbAstDiskCache {

// 缓存格式版本（改动 AST 布局 / 解析语义时 +1，旧缓存自动失效）
constexpr int kFormatVersion = 1;

// 由装载输入算出缓存 key（十六进制）。key 已包含上面列出的全部失效因子。
[[nodiscard]] QString computeKey(const QString& dirPath, const QStringList& files,
                                 int readEncoding, bool debugMode);

// ---- 单个解析结果的 blob（在 worker 线程上调用，可并行）----
[[nodiscard]] QByteArray serializeParsedFile(const ParsedErbFile& pf);
bool deserializeParsedFile(const QByteArray& blob, ParsedErbFile& out);

// ---- 增量写（边解析边落盘，内存不随装载规模增长）----
class Writer {
public:
    explicit Writer(const QString& key);
    ~Writer();
    bool begin(int fileCount);              // 打开并写头
    bool writeBlob(const QByteArray& blob); // 追加一个 ParsedErbFile
    [[nodiscard]] bool isOpen() const { return m_open; }
    bool commit();                          // 校验并原子落盘
    void abort();                           // 放弃（删除临时文件）
private:
    QString m_path;
    QSaveFile m_file;
    QDataStream m_out;
    bool m_open = false;
    bool m_failed = false;
};

// ---- 增量读（一次一个 ParsedErbFile，避免整库常驻）----
class Reader {
public:
    explicit Reader(const QString& key);
    [[nodiscard]] bool isOpen() const { return m_open; }
    [[nodiscard]] int count() const { return m_count; }
    // 只读出下一个文件的 blob（不解码）—— 便于用线程池并行解码。
    bool readBlob(QByteArray& out);
    bool next(ParsedErbFile& out);
    void close();
private:
    QFile m_file;
    QDataStream m_in;
    bool m_open = false;
    int m_count = 0;
    int m_read = 0;
};

// 删除某 key 的缓存。
void remove(const QString& key);

// 缓存启用开关：默认**关**，可由环境变量 EMUERA_AST_DISK_CACHE=1/true/on 打开，
// 或由 ErbLoader::setAstDiskCache(true) 打开。
[[nodiscard]] bool enabled();
void setEnabled(bool on);

}  // namespace ErbAstDiskCache

#endif   // ERA_ERB_AST_DISK_CACHE_H
