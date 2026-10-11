#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace EraBinaryData {
// EraBinaryDataReader/Writer version 1808. Dimensions exclude the character index.
enum class FileType : quint8 { Normal = 0, Global = 1, Var = 2, Character = 3 };
struct Variable {
    QString name;
    bool string = false;
    QList<int> dimensions; // empty = scalar; one to three entries = rectangular array
    QList<qint64> integers;
    QList<QString> strings;
};
struct Character {
    QList<Variable> builtins;
    QList<Variable> userDefined;
};
struct Document {
    FileType type = FileType::Normal;
    qint64 uniqueCode = 0;
    qint64 scriptVersion = 0;
    QString description;
    QList<Character> characters;
    QList<Variable> variables;
};
// Native EraDataStream 1808 text (normal and GLOBAL); schema selects the normal/GLOBAL container.
bool encodeText(const Document& document, QByteArray& bytes, QString* error = nullptr);
bool decodeText(const QByteArray& bytes, const Document& schema, Document& document, QString* error = nullptr);
// Failure never changes the destination. No legacy port formats are accepted.
bool decode(const QByteArray& bytes, Document& document, QString* error = nullptr);
bool encode(const Document& document, QByteArray& bytes, QString* error = nullptr, bool gzip = false);
}
