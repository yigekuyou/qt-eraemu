#ifndef LABEL_DICTIONARY_H
#define LABEL_DICTIONARY_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QMap>
#include "script_line.h"

class LabelDictionary : public QObject
{
    Q_OBJECT

public:
    explicit LabelDictionary(QObject *parent = nullptr);

    // Label management
    Q_INVOKABLE void addLabel(const QString &name, int lineNumber);
    Q_INVOKABLE void addLabelWithFile(const QString &name, int lineNumber, const QString &fileName);
    
    Q_INVOKABLE int getLabelLine(const QString &name) const;
    Q_INVOKABLE QString getLabelFile(const QString &name) const;
    
    Q_INVOKABLE bool hasLabel(const QString &name) const;
    Q_INVOKABLE void clear();

    // Label validation
    Q_INVOKABLE bool isValidLabelName(const QString &name) const;
    
    Q_INVOKABLE ScriptPosition findLabel(const QString &name) const;

    // Get all labels for debugging
    Q_INVOKABLE QStringList getAllLabelNames() const;
    Q_INVOKABLE QMap<QString, int> getAllLabels() const;
    Q_INVOKABLE QMap<QString, QString> getAllLabelFiles() const;

private:
    QHash<QString, int> m_labels;
    QHash<QString, QString> m_labelFiles;
};

#endif // LABEL_DICTIONARY_H
