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
#include <QVariant>
#include <QDir>

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
    QString findActualDir(const QString& basePath, const QString& targetName) const;
    QString getPathWithActualCase(const QString& basePath, const QString& targetPath) const;

    // 解析子目录的**绝对路径**（大小写不敏感）；不存在返回空字符串。
    // 对齐 C# 的 Program.ErbDir / Program.CsvDir（只在该目录内检索）。
    QString resolveSubDir(const QString& basePath, const QString& name) const;
    QString resolveSubDir(const QString& basePath, const QStringList& names) const;

    // 枚举目录下的文件（大小写不敏感的后缀过滤），recursive = 搜索子目录（Config.SearchSubdirectory）
    QStringList listFiles(const QString& dirPath, const QStringList& suffixes, bool recursive) const;
    
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
    
    // Era binary format support
    IoResult readEraData(const QString& filePath, QVariantMap& data);
    IoResult writeEraData(const QString& filePath, const QVariantMap& data);

private:
    // Helper methods
    bool isEraBinaryFormat(const QByteArray& data) const;
};

#endif // FILE_SYSTEM_IO_H