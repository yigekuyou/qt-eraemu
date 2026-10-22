#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace EraBinaryData {
// 存档容器版本的**唯一副本**：二进制头写入、decode() 校验、文本起始标记里的
// 数字都由它派生。1808 是 C# Emuera 的容器版本；本移植把私有变量作用域身份
// 从「函数名」改成「段身份 (脚本, 段起点行)」之后，旧的私有条目名无法再还原
// 到正确的变量，旧档整体判废（当前程序尚未稳定，不做迁移、不加扩展计数）。
// 因此版本提到 1809：版本字段不是本值的档在 decode()/decodeText() 处**原子
// 失败**（目标 document 不变），诊断文案明确要求删除旧档。改这个数字 = 主动
// 让所有既有存档失效；不要为了「兼容读」在校验处分叉出第二个版本集合。
inline constexpr quint32 Version = 1809;
// Dimensions exclude the character index.
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
// Native EraDataStream text (normal and GLOBAL); schema selects the normal/GLOBAL container.
// 文本档的版本写在起始标记里（"__EMUERA_<Version>_STRAT__"），与二进制头同源。
bool encodeText(const Document& document, QByteArray& bytes, QString* error = nullptr);
bool decodeText(const QByteArray& bytes, const Document& schema, Document& document, QString* error = nullptr);
// Failure never changes the destination. No legacy port formats are accepted.
bool decode(const QByteArray& bytes, Document& document, QString* error = nullptr);
bool encode(const Document& document, QByteArray& bytes, QString* error = nullptr, bool gzip = false);
}
