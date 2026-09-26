#ifndef IDENTIFIER_DICTIONARY_H
#define IDENTIFIER_DICTIONARY_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include <QtQml/qqmlregistration.h>

// Identifier type enum
enum class DefinedNameType {
    None = 0,
    Reserved,
    SystemVariable,
    SystemMethod,
    SystemInstrument,
    UserGlobalVariable,
    UserMacro,
    UserRefMethod,
    NameSpace,
};

// IdentifierDictionary class
class IdentifierDictionary : public QObject
{
    Q_OBJECT
public:
    explicit IdentifierDictionary(QObject *parent = nullptr);

    // Name checking methods
    Q_INVOKABLE bool isReserved(const QString &name) const;
    Q_INVOKABLE bool isSystemVariable(const QString &name) const;
    Q_INVOKABLE bool isSystemMethod(const QString &name) const;
    Q_INVOKABLE bool isSystemInstrument(const QString &name) const;

    // User name checking (for labels, variables, macros)
    Q_INVOKABLE bool isUserGlobalVariable(const QString &name) const;
    Q_INVOKABLE bool isUserMacro(const QString &name) const;
    Q_INVOKABLE bool isUserRefMethod(const QString &name) const;

    // Check if name is in use
    Q_INVOKABLE bool isNameInUse(const QString &name) const;

    // Add user definitions
    void addUserGlobalVariable(const QString &name);
    void addUserMacro(const QString &name);
    void addUserRefMethod(const QString &name);

    // Check reserved words
    Q_INVOKABLE bool isReservedWord(const QString &name) const;

    // Check system names
    Q_INVOKABLE bool isSystemName(const QString &name) const;

private:
    // Name dictionary
    QHash<QString, DefinedNameType> m_nameDic;

    // Reserved words
    static const QList<QString> RESERVED_WORDS;

    // System names (instructions, methods, variables)
    static const QList<QString> SYSTEM_INSTRUMENTS;
    static const QList<QString> SYSTEM_METHODS;
    static const QList<QString> SYSTEM_VARIABLES;

    // User-defined names
    QList<QString> m_userGlobalVariables;
    QList<QString> m_userMacros;
    QList<QString> m_userRefMethods;

    // Initialize reserved words
    void initializeReservedWords();

    // Initialize system names
    void initializeSystemNames();
};

#endif // IDENTIFIER_DICTIONARY_H
