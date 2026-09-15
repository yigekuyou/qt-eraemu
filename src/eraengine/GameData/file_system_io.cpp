#include "file_system_io.h"
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

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

QString FileSystem::findActualDir(const QString& basePath, const QString& targetName) const
{
		QDir dir(basePath);
    if (!dir.exists()) {
				qDebug() << "Game directory not exists:" << basePath;
    }

    QStringList entries = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QString& subDir : entries) {
        if (subDir.compare(targetName, Qt::CaseInsensitive) == 0) {
            return subDir;
        }
    }
    return targetName;
}

QString FileSystem::getPathWithActualCase(const QString& basePath, const QString& targetPath) const
{
    // Split the path into components
    QDir baseDir(basePath);
    if (!baseDir.exists()) {
        qDebug() << "Base path does not exist:" << basePath;
        return targetPath;
    }

    QFileInfo targetInfo(targetPath);
    QString relativePath = baseDir.relativeFilePath(targetPath);
    
    if (relativePath.isEmpty() || relativePath == ".") {
        return basePath;
    }

    // Split into components
    QStringList components = relativePath.split('/', Qt::SkipEmptyParts);
    QStringList actualComponents;
    
    QDir currentDir = baseDir;
    for (const QString& component : components) {
        QString actualName = findActualDir(currentDir.absolutePath(), component);
        actualComponents.append(actualName);
        currentDir.cd(actualName);
    }
    
    return currentDir.absolutePath();
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
    QFileInfo fileInfo(filePath);
    return fileInfo.absolutePath();
}

QString FileSystem::combinePath(const QString& basePath, const QString& relativePath) const
{
    QDir baseDir(basePath);
    return baseDir.absoluteFilePath(relativePath);
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

IoResult BinaryIo::readEraData(const QString& filePath, QVariantMap& data)
{
    // Placeholder for Era data reading
    return IoResult(true, "Era data reading not implemented");
}

IoResult BinaryIo::writeEraData(const QString& filePath, const QVariantMap& data)
{
    // Placeholder for Era data writing
    return IoResult(true, "Era data writing not implemented");
}

bool BinaryIo::isEraBinaryFormat(const QByteArray& data) const
{
    // Placeholder for Era binary format detection
    return false;
}
