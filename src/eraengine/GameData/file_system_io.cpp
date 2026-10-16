#include "game_paths.h"
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
#include "file_system_io.h"
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <functional>
#include <QMutex>
#include <QMutexLocker>
#include <QtConcurrent/QtConcurrentMap>

FileSystem::FileSystem(QObject *parent)
    : QObject(parent)
{
    m_rootDir = QString();
    m_lastError = QString();
}

void FileSystem::setRootDir(const QString& rootDir)
{
    m_rootDir = rootDir;
}

QString FileSystem::getRootDir() const
{
    return m_rootDir;
}

QString FileSystem::findActualDir(const QString& basePath, const QString& targetName)
{
    QDir dir(basePath);
    if (QFileInfo::exists(dir.filePath(targetName))) return targetName;
    const QStringList entries = dir.entryList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& entry : entries) {
        if (entry.compare(targetName, Qt::CaseInsensitive) == 0) return entry;
    }
    return targetName;
}

QString FileSystem::resolveSubDir(const QString& basePath, const QString& name) const
{
    QDir base(basePath);
    if (!base.exists()) {
        return QString();
    }
    const QStringList entries = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& e : entries) {
        if (e.compare(name, Qt::CaseInsensitive) == 0) {
            return GamePaths::join(basePath, e);
        }
    }
    return QString();
}

QString FileSystem::resolveSubDir(const QString& basePath, const QStringList& names) const
{
    for (const QString& n : names) {
        const QString p = resolveSubDir(basePath, n);
        if (!p.isEmpty()) return p;
    }
    return QString();
}

QStringList FileSystem::listFiles(const QString& dirPath, const QStringList& suffixes,
                                  bool recursive) const
{
    QStringList out;
    QDir dir(dirPath);
    if (!dir.exists()) return out;

    const QDir::Filters fileFilter = QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot;
    const auto matches = [&suffixes](const QString& fileName) {
        for (const QString& raw : suffixes) {
            // 允许 "*.csv" 或 ".csv" 两种写法
            const QString s = raw.startsWith(QLatin1Char('*')) ? raw.mid(1) : raw;
            if (s.isEmpty()) continue;
            if (fileName.endsWith(s, Qt::CaseInsensitive)) return true;
        }
        return false;
    };

    const QFileInfoList files = dir.entryInfoList(fileFilter, QDir::Name);
    for (const QFileInfo& fi : files) {
        if (matches(fi.fileName())) out.append(fi.absoluteFilePath());
    }
    if (recursive) {
        const QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& d : dirs) {
            out += listFiles(GamePaths::join(dirPath, d), suffixes, true);
        }
    }
    return out;
}

QString FileSystem::getPathWithActualCase(const QString& basePath, const QString& targetPath)
{
    return resolvePathCase(GamePaths::join(basePath, targetPath));
}

// ---------------------------------------------------------------------------
// 路径树：预扫描一次的目录清单快照（见 file_system_io.h 的 DirNode 注释）。
// 每个目录只做一次 AllEntries listing，文件/子目录在内存里分拣 ——
// 同 ErbLoader::collectFiles / loadCharaDirectory 的教训：一次列举就是
// 一整趟 provider 往返（对 content://）或一次系统调用（本地），分
// 「文件一次 + 目录一次」会让每个目录白跑一趟。
namespace {

// 列出单个目录：文件进 files，子目录进 subdirs（**一次性 resize 预留槽位**，
// 之后不再追加 —— 元素地址因此稳定，上层才能把指针交给下一层并行任务填充；
// QVector 追加会搬移元素）。顺序即 entryInfoList 的 QDir::Name 名字序。
void listSingleDir(const QString& dirPath, DirNode& node, QVector<DirNode*>& kids)
{
    const QFileInfoList entries = QDir(dirPath).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    int dirCount = 0;
    for (const QFileInfo& e : entries)
        if (e.isDir()) ++dirCount;
    node.subdirs.resize(dirCount);
    int di = 0;
    for (const QFileInfo& e : entries) {
        if (e.isDir()) {
            DirNode& sub = node.subdirs[di++];
            sub.path = e.absoluteFilePath();
            kids.append(&sub);
        } else if (e.isFile()) {
            DirEntry entry;
            entry.name = e.fileName();
            entry.path = e.absoluteFilePath();
            node.files.append(entry);
        }
    }
}

} // namespace

DirNode FileSystem::collectTree(const QString& dirPath, const TreeScanProgress& onProgress)
{
    DirNode root;
    root.path = dirPath;
    // 不做 QDir::exists()：对 content:// 它是一次独立的 provider 查询，
    // 而 entryInfoList 对不存在的目录本就返回空列表（同样得到空树）。
    // 每目录省一趟 = 38 目录少 38 次往返（eraH 实测约一半扫描时间）。
    //
    // 按层 BFS + **层内并行**：同一层的目录互不依赖，一次 QtConcurrent::map
    // 把它们全部列出。SAF 上每次 listing 都是一次 provider 往返（本地是系统
    // 调用），eraH 的 38 个目录里同层最多 ~18 个 —— 并行把墙钟时间从
    // 「Σ 每目录延迟」压到「最慢一层」。
    QVector<DirNode*> level{&root};
    int done = 0;
    while (!level.isEmpty()) {
        QVector<DirNode*> next;
        QMutex mutex;
        QFuture<void> f = QtConcurrent::map(level, [&](DirNode* n) {
            QVector<DirNode*> kids;
            listSingleDir(n->path, *n, kids);
            QMutexLocker lock(&mutex);
            next += kids;
        });
        f.waitForFinished();
        done += level.size();
        if (onProgress) onProgress(done, done + next.size());
        level = next;
    }
    return root;
}

// 纯内存查树：与 FileSystem::listFiles 相同的后缀匹配器（允许 "*.csv"/".csv"
// 两种写法、大小写不敏感），但匹配 DirEntry::name、返回 DirEntry::path，
// 全程零 provider 往返。
QStringList FileSystem::listFilesFromTree(const DirNode& root,
                                         const QStringList& suffixes,
                                         bool recursive)
{
    QStringList out;
    const auto matches = [&suffixes](const QString& fileName) {
        for (const QString& raw : suffixes) {
            const QString s = raw.startsWith(QLatin1Char('*')) ? raw.mid(1) : raw;
            if (s.isEmpty()) continue;
            if (fileName.endsWith(s, Qt::CaseInsensitive)) return true;
        }
        return false;
    };
    const std::function<void(const DirNode&)> walk = [&](const DirNode& n) {
        for (const DirEntry& f : n.files)
            if (matches(f.name)) out.append(f.path);
        if (!recursive) return;
        for (const DirNode& sub : n.subdirs)
            walk(sub);
    };
    walk(root);
    return out;
}


QString FileSystem::resolvePathCase(const QString& path)
{
    if (path.isEmpty() || GamePaths::isContent(path)) return path;
    const QFileInfo info(path);
    if (info.exists()) return info.absoluteFilePath();
    // Resolve parents without assuming a Unix root: QFileInfo retains Windows
    // drive roots and UNC shares. Stop when the parent no longer changes.
    const QString absolute = info.absoluteFilePath();
    const QString parent = info.absolutePath();
    if (parent == absolute || info.fileName().isEmpty()) return absolute;
    const QString actualParent = resolvePathCase(parent);
    return QDir(actualParent).filePath(findActualDir(actualParent, info.fileName()));
}

IoResult FileSystem::readFile(const QString& filePath, QString& content)
{
    if (!validateFilePath(filePath)) {
        return IoResult(false, QString("Invalid file path: %1").arg(filePath));
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = QString("Failed to open file: %1").arg(file.errorString());
        return IoResult(false, m_lastError);
    }
    
    QTextStream in(&file);
    content = in.readAll();
    file.close();
    
    return IoResult(true, QString("Successfully read file: %1").arg(filePath), content.size());
}

IoResult FileSystem::writeFile(const QString& filePath, const QString& content)
{
    if (!validateFilePath(filePath)) {
        return IoResult(false, QString("Invalid file path: %1").arg(filePath));
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastError = QString("Failed to write file: %1").arg(file.errorString());
        return IoResult(false, m_lastError);
    }
    
    QTextStream out(&file);
    out << content;
    file.close();
    
    return IoResult(true, QString("Successfully wrote file: %1").arg(filePath), content.size());
}

IoResult FileSystem::appendFile(const QString& filePath, const QString& content)
{
    if (!validateFilePath(filePath)) {
        return IoResult(false, QString("Invalid file path: %1").arg(filePath));
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        m_lastError = QString("Failed to append to file: %1").arg(file.errorString());
        return IoResult(false, m_lastError);
    }
    
    QTextStream out(&file);
    out << content;
    file.close();
    
    return IoResult(true, QString("Successfully appended to file: %1").arg(filePath), content.size());
}

bool FileSystem::createDirectory(const QString& dirPath)
{
    if (!validateDirectoryPath(dirPath)) {
        m_lastError = QString("Invalid directory path: %1").arg(dirPath);
        return false;
    }
    
    QDir dir(dirPath);
    return dir.mkpath(dirPath);
}

bool FileSystem::deleteDirectory(const QString& dirPath)
{
    if (!validateDirectoryPath(dirPath)) {
        m_lastError = QString("Invalid directory path: %1").arg(dirPath);
        return false;
    }
    
    QDir dir(dirPath);
    return dir.removeRecursively();
}

QStringList FileSystem::listDirectory(const QString& dirPath)
{
    if (!validateDirectoryPath(dirPath)) {
        return QStringList();
    }
    
    QDir dir(dirPath);
    return dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
}

bool FileSystem::directoryExists(const QString& dirPath) const
{
    QDir dir(dirPath);
    return dir.exists();
}

bool FileSystem::fileExists(const QString& filePath) const
{
    QFileInfo fileInfo(filePath);
    return fileInfo.exists() && fileInfo.isFile();
}

qint64 FileSystem::getFileSize(const QString& filePath) const
{
    QFileInfo fileInfo(filePath);
    return fileInfo.size();
}

QString FileSystem::getFileExtension(const QString& filePath) const
{
    QFileInfo fileInfo(filePath);
    return fileInfo.suffix();
}

QString FileSystem::getFileName(const QString& filePath) const
{
    QFileInfo fileInfo(filePath);
    return fileInfo.fileName();
}

QString FileSystem::normalizePath(const QString& path) const
{
    if (GamePaths::isContent(path)) return path;
    if (GamePaths::isContent(m_rootDir)) return GamePaths::join(m_rootDir, path);
    if (m_rootDir.isEmpty()) {
        QFileInfo fileInfo(path);
        return fileInfo.canonicalFilePath();
    }
    
    // If path is relative, combine with root directory
    QFileInfo fileInfo(path);
    if (fileInfo.isRelative()) {
        return QDir(m_rootDir).absoluteFilePath(path);
    }
    
    // If path is absolute, return as is
    return fileInfo.canonicalFilePath();
}

QString FileSystem::getDirectoryPath(const QString& filePath) const
{
    // SAF documents have opaque IDs and no portable parent URI. Callers must
    // retain the directory used to enumerate the document instead.
    if (GamePaths::isContent(filePath)) return {};
    QFileInfo fileInfo(filePath);
    return fileInfo.absolutePath();
}

QString FileSystem::combinePath(const QString& basePath, const QString& relativePath) const
{
    return GamePaths::join(basePath, relativePath);
}

IoResult FileSystem::readTextFile(const QString& filePath, QString& content)
{
    return readFile(filePath, content);
}

IoResult FileSystem::writeTextFile(const QString& filePath, const QString& content)
{
    return writeFile(filePath, content);
}

IoResult FileSystem::readBinaryFile(const QString& filePath, QByteArray& content)
{
    if (!validateFilePath(filePath)) {
        return IoResult(false, QString("Invalid file path: %1").arg(filePath));
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QString("Failed to read binary file: %1").arg(file.errorString());
        return IoResult(false, m_lastError);
    }
    
    content = file.readAll();
    file.close();
    
    return IoResult(true, QString("Successfully read binary file: %1").arg(filePath), content.size());
}

IoResult FileSystem::writeBinaryFile(const QString& filePath, const QByteArray& content)
{
    if (!validateFilePath(filePath)) {
        return IoResult(false, QString("Invalid file path: %1").arg(filePath));
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = QString("Failed to write binary file: %1").arg(file.errorString());
        return IoResult(false, m_lastError);
    }
    
    file.write(content);
    file.close();
    
    return IoResult(true, QString("Successfully wrote binary file: %1").arg(filePath), content.size());
}

QString FileSystem::lastError() const
{
    return m_lastError;
}

void FileSystem::clearError()
{
    m_lastError = QString();
}

bool FileSystem::validateFilePath(const QString& filePath) const
{
    if (filePath.isEmpty()) {
        return false;
    }
    
    // Simple validation - check for dangerous patterns
    QRegularExpression re(R"([<>'"\\])");
    return !re.match(filePath).hasMatch();
}

bool FileSystem::validateDirectoryPath(const QString& dirPath) const
{
    if (dirPath.isEmpty()) {
        return false;
    }
    
    // Simple validation
    QRegularExpression re(R"([<>'"\\])");
    return !re.match(dirPath).hasMatch();
}

LexicalAnalyzer::LexicalAnalyzer(QObject *parent)
    : QObject(parent)
{
    initializeKeywords();
}

void LexicalAnalyzer::initializeKeywords()
{
    // Initialize common keywords
    m_keywords["IF"] = Keyword;
    m_keywords["ELSE"] = Keyword;
    m_keywords["ENDIF"] = Keyword;
    m_keywords["FOR"] = Keyword;
    m_keywords["WHILE"] = Keyword;
    m_keywords["GOTO"] = Keyword;
    m_keywords["CALL"] = Keyword;
    m_keywords["RETURN"] = Keyword;
    m_keywords["SET"] = Keyword;
    m_keywords["PRINT"] = Keyword;
    m_keywords["INPUT"] = Keyword;
    m_keywords["FUNCTION"] = Keyword;
    m_keywords["ENDFUNCTION"] = Keyword;
}

QStringList LexicalAnalyzer::tokenize(const QString& input)
{
    QStringList tokens;
    QString currentToken;
    bool inString = false;
    
    for (int i = 0; i < input.length(); ++i) {
        QChar ch = input[i];
        
        if (ch == '"') {
            inString = !inString;
            currentToken += ch;
            if (!inString) {
                tokens.append(currentToken);
                currentToken.clear();
            }
        } else if (inString || !ch.isSpace()) {
            currentToken += ch;
        } else if (!currentToken.isEmpty()) {
            tokens.append(currentToken);
            currentToken.clear();
        }
    }
    
    if (!currentToken.isEmpty()) {
        tokens.append(currentToken);
    }
    
    return tokens;
}

QStringList LexicalAnalyzer::tokenizeFile(const QString& filePath)
{
    QString content;
    FileSystem fs;
    IoResult result = fs.readFile(filePath, content);
    
    if (!result.success) {
        return QStringList();
    }
    
    return tokenize(content);
}

QList<LexicalAnalyzer::Token> LexicalAnalyzer::analyzeTokens(const QString& input)
{
    QList<Token> tokens;
    QStringList rawTokens = tokenize(input);
    int line = 1;
    int column = 0;
    
    for (const QString& rawToken : rawTokens) {
        TokenType type = getTokenType(rawToken);
        tokens.append(Token(type, rawToken, line, column));
        column += rawToken.length() + 1; // +1 for space
    }
    
    return tokens;
}

QList<LexicalAnalyzer::Token> LexicalAnalyzer::analyzeFile(const QString& filePath)
{
    QString content;
    FileSystem fs;
    IoResult result = fs.readFile(filePath, content);
    
    if (!result.success) {
        return QList<Token>();
    }
    
    return analyzeTokens(content);
}

LexicalAnalyzer::TokenType LexicalAnalyzer::getTokenType(const QString& value) const
{
    if (m_keywords.contains(value.toUpper())) {
        return m_keywords[value.toUpper()];
    }
    
    if (value.startsWith('"') && value.endsWith('"')) {
        return StringLiteral;
    }
    
    if (value.contains(QRegularExpression(R"(\d+)")) && !value.contains(QRegularExpression(R"([a-zA-Z])"))) {
        return Number;
    }
    
    if (value.contains(QRegularExpression(R"([a-zA-Z_][a-zA-Z0-9_]*)"))) {
        return Identifier;
    }
    
    return Unknown;
}

BinaryIo::BinaryIo(QObject *parent)
    : QObject(parent)
{
}

IoResult BinaryIo::readBinary(const QString& filePath, QByteArray& data)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return IoResult(false, QString("Failed to read binary file: %1").arg(file.errorString()));
    }
    
    data = file.readAll();
    file.close();
    
    return IoResult(true, QString("Successfully read binary file: %1").arg(filePath), data.size());
}

quint8 BinaryIo::readByte(const QByteArray& data, int offset)
{
    if (offset >= 0 && offset < data.size()) {
        return static_cast<quint8>(data[offset]);
    }
    return 0;
}

quint16 BinaryIo::readUInt16(const QByteArray& data, int offset)
{
    if (offset + 1 < data.size()) {
        quint16 value = 0;
        value |= (static_cast<quint8>(data[offset]) << 8);
        value |= static_cast<quint8>(data[offset + 1]);
        return value;
    }
    return 0;
}

quint32 BinaryIo::readUInt32(const QByteArray& data, int offset)
{
    if (offset + 3 < data.size()) {
        quint32 value = 0;
        value |= (static_cast<quint32>(static_cast<quint8>(data[offset])) << 24);
        value |= (static_cast<quint32>(static_cast<quint8>(data[offset + 1])) << 16);
        value |= (static_cast<quint32>(static_cast<quint8>(data[offset + 2])) << 8);
        value |= static_cast<quint32>(static_cast<quint8>(data[offset + 3]));
        return value;
    }
    return 0;
}

qint32 BinaryIo::readInt32(const QByteArray& data, int offset)
{
    if (offset + 3 < data.size()) {
        qint32 value = 0;
        value |= (static_cast<qint32>(static_cast<quint8>(data[offset])) << 24);
        value |= (static_cast<qint32>(static_cast<quint8>(data[offset + 1])) << 16);
        value |= (static_cast<qint32>(static_cast<quint8>(data[offset + 2])) << 8);
        value |= static_cast<qint32>(static_cast<quint8>(data[offset + 3]));
        return value;
    }
    return 0;
}

IoResult BinaryIo::writeBinary(const QString& filePath, const QByteArray& data)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return IoResult(false, QString("Failed to write binary file: %1").arg(file.errorString()));
    }
    
    file.write(data);
    file.close();
    
    return IoResult(true, QString("Successfully wrote binary file: %1").arg(filePath), data.size());
}

QByteArray BinaryIo::writeByte(quint8 value)
{
    QByteArray data;
    data.append(static_cast<char>(value));
    return data;
}

QByteArray BinaryIo::writeUInt16(quint16 value)
{
    QByteArray data;
    data.append(static_cast<char>(value >> 8));
    data.append(static_cast<char>(value & 0xFF));
    return data;
}

QByteArray BinaryIo::writeUInt32(quint32 value)
{
    QByteArray data;
    data.append(static_cast<char>(value >> 24));
    data.append(static_cast<char>((value >> 16) & 0xFF));
    data.append(static_cast<char>((value >> 8) & 0xFF));
    data.append(static_cast<char>(value & 0xFF));
    return data;
}

QString FileSystem::getConfigPath(const QString& basePath, const QString& configName) const
{
    // 对齐 C#：配置文件（_default.config / _fixed.config / _Rename.csv / _Replace.csv）
    // 位于 CSV 目录内
    const QString csvDir = resolveSubDir(basePath, QStringList{"CSV", "csv"});
    if (csvDir.isEmpty()) {
        return QString();
    }
    const QString path = GamePaths::join(csvDir, configName);
    return QFile::exists(path) ? path : QString();
}
