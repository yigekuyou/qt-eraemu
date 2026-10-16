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
#ifndef FILE_SYSTEM_IO_H
#define FILE_SYSTEM_IO_H

#include <QObject>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QVariant>
#include <QDir>
#include <QVector>
#include <functional>

// Basic I/O operation result
struct IoResult {
    bool success;
    QString message;
    qint64 bytesProcessed;
    
    IoResult(bool succ, const QString& msg, qint64 bytes = 0)
        : success(succ), message(msg), bytesProcessed(bytes) {}
};

// File access mode
enum class FileAccessMode {
    Read,
    Write,
    ReadWrite,
    Append
};

// ---------------------------------------------------------------------------
// 路径树 —— 一次预扫描形成的目录清单快照。
// Android SAF（content://）下每次目录列举都是 DocumentsProvider 跨进程往返
// （实测 ~0.5-1s/目录，见 docs/android-saf.md）；此前常量表、Item.csv 查找、
// 角色 CSV 各自独立遍历同一棵树，三趟 ~190 次往返是加载耗时的大头。
// collectTree() 走一遍形成树后，listFilesFromTree() / 常量表装载 / 角色 CSV
// 装载都查这棵内存树，不再触碰 provider。
// 每个目录只做**一次** AllEntries listing，文件/子目录在内存里分拣；
// 条目同时缓存显示名与完整路径 —— 后者来自 iterator 的 absoluteFilePath()，
// 可直接打开，不必再走 GamePaths::join（join 对 content:// 每文件都要一次
// exists 探测，~51ms/次，1328 个文件就是一分钟）。
struct DirEntry {
    QString name;   // 显示名（entryInfoList 的 fileName()，纯字符串，零探测）
    QString path;   // 完整路径/URI（absoluteFilePath()，可直接读取）
};
struct DirNode {
    QString path;              // 目录完整路径/URI
    QList<DirEntry> files;     // 本目录文件（QDir::Name 序）
    QVector<DirNode> subdirs;  // 子目录（QDir::Name 序）
    [[nodiscard]] bool isEmpty() const { return files.isEmpty() && subdirs.isEmpty(); }
};

// File system interface
class FileSystem : public QObject
{
    Q_OBJECT

public:
    explicit FileSystem(QObject *parent = nullptr);
    
    // Directory management
    void setRootDir(const QString& rootDir);
    QString getRootDir() const;
    
    // Case-insensitive directory lookup
    static QString findActualDir(const QString& basePath, const QString& targetName);
    static QString getPathWithActualCase(const QString& basePath, const QString& targetPath);
    // Exact paths win; otherwise resolve each missing component case-insensitively.
    static QString resolvePathCase(const QString& path);

    // 解析子目录的**绝对路径**（大小写不敏感）；不存在返回空字符串。
    // 对齐 C# 的 Program.ErbDir / Program.CsvDir（只在该目录内检索）。
    QString resolveSubDir(const QString& basePath, const QString& name) const;
    QString resolveSubDir(const QString& basePath, const QStringList& names) const;

    // 枚举目录下的文件（大小写不敏感的后缀过滤），recursive = 搜索子目录（Config.SearchSubdirectory）
    QStringList listFiles(const QString& dirPath, const QStringList& suffixes, bool recursive) const;

    // 扫描进度回调：done = 已完成 listing 的目录数，total = done + 已发现待扫描
    // 的目录数（目录数逐层发现，total 会随扫描推进增长）。在**调用线程**触发。
    using TreeScanProgress = std::function<void(int done, int total)>;

    // 预扫描目录树（递归收集所有子目录；目录不存在返回空树，path 已填充）。
    // 每目录一次 AllEntries listing；**按层并行**（同层目录互不依赖）；
    // 返回的树供后续装载阶段共享，见 DirNode 注释。
    [[nodiscard]] static DirNode collectTree(const QString& dirPath,
                                            const TreeScanProgress& onProgress = {});

    // 与 listFiles(dirPath, suffixes, recursive) 语义一致的纯内存查树（零 provider 往返）：
    // 按显示名做大小写不敏感的后缀匹配，返回条目的完整路径（DirEntry::path）。
    [[nodiscard]] static QStringList listFilesFromTree(const DirNode& root,
                                                       const QStringList& suffixes,
                                                       bool recursive);
    
    // File operations
    IoResult readFile(const QString& filePath, QString& content);
    IoResult writeFile(const QString& filePath, const QString& content);
    IoResult appendFile(const QString& filePath, const QString& content);
    
    // Directory operations
    bool createDirectory(const QString& dirPath);
    bool deleteDirectory(const QString& dirPath);
    QStringList listDirectory(const QString& dirPath);
    bool directoryExists(const QString& dirPath) const;
    bool fileExists(const QString& filePath) const;
    
    // File information
    qint64 getFileSize(const QString& filePath) const;
    QString getFileExtension(const QString& filePath) const;
    QString getFileName(const QString& filePath) const;
    
    // Path operations
    QString normalizePath(const QString& path) const;
    QString getDirectoryPath(const QString& filePath) const;
    QString combinePath(const QString& basePath, const QString& relativePath) const;
    
    // Config file path helpers
    QString getConfigPath(const QString& basePath, const QString& configName) const;
    
    // Stream operations
    IoResult readTextFile(const QString& filePath, QString& content);
    IoResult writeTextFile(const QString& filePath, const QString& content);
    
    // Binary operations
    IoResult readBinaryFile(const QString& filePath, QByteArray& content);
    IoResult writeBinaryFile(const QString& filePath, const QByteArray& content);
    
    // Error handling
    QString lastError() const;
    void clearError();

private:
    QString m_rootDir;
    QString m_lastError;
    
    // Helper methods
    bool validateFilePath(const QString& filePath) const;
    bool validateDirectoryPath(const QString& dirPath) const;
};

// Lexical analyzer for tokenizing input
class LexicalAnalyzer : public QObject
{
    Q_OBJECT

public:
    explicit LexicalAnalyzer(QObject *parent = nullptr);
    
    // Tokenization
    QStringList tokenize(const QString& input);
    QStringList tokenizeFile(const QString& filePath);
    
    // Token types
    enum TokenType {
        Identifier,
        Number,
        StringLiteral,
        Operator,
        Keyword,
        Punctuation,
        Whitespace,
        Comment,
        Unknown
    };
    
    // Token structure
    struct Token {
        TokenType type;
        QString value;
        int line;
        int column;
        
        Token(TokenType t, const QString& v, int l = 0, int c = 0)
            : type(t), value(v), line(l), column(c) {}
    };
    
    // Token analysis
    QList<Token> analyzeTokens(const QString& input);
    QList<Token> analyzeFile(const QString& filePath);
    
private:
    QHash<QString, TokenType> m_keywords;
    
    // Helper methods
    void initializeKeywords();
    TokenType getTokenType(const QString& value) const;
};

// Binary data I/O operations
class BinaryIo : public QObject
{
    Q_OBJECT

public:
    explicit BinaryIo(QObject *parent = nullptr);
    
    // Binary reading
    IoResult readBinary(const QString& filePath, QByteArray& data);
    quint8 readByte(const QByteArray& data, int offset);
    quint16 readUInt16(const QByteArray& data, int offset);
    quint32 readUInt32(const QByteArray& data, int offset);
    qint32 readInt32(const QByteArray& data, int offset);
    
    // Binary writing
    IoResult writeBinary(const QString& filePath, const QByteArray& data);
    QByteArray writeByte(quint8 value);
    QByteArray writeUInt16(quint16 value);
    QByteArray writeUInt32(quint32 value);

private:
};

#endif // FILE_SYSTEM_IO_H