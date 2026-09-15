#include "label_dictionary.h"
#include <QMap>

LabelDictionary::LabelDictionary(QObject *parent)
    : QObject(parent)
{
}

void LabelDictionary::addLabel(const QString &name, int lineNumber)
{
    // Store the label with its line number
    m_labels.insert(name, lineNumber);
}

void LabelDictionary::addLabelWithFile(const QString &name, int lineNumber, const QString &fileName)
{
    // Store the label with its line number and file
    m_labels.insert(name, lineNumber);
    m_labelFiles.insert(name, fileName);
}

int LabelDictionary::getLabelLine(const QString &name) const
{
    // Return the line number for the label, or -1 if not found
    if (m_labels.contains(name)) {
        return m_labels.value(name);
    }
    return -1;
}

QString LabelDictionary::getLabelFile(const QString &name) const
{
    // Return the file name for the label, or empty if not found
    return m_labelFiles.value(name, "");
}

bool LabelDictionary::hasLabel(const QString &name) const
{
    return m_labels.contains(name);
}

void LabelDictionary::clear()
{
    m_labels.clear();
    m_labelFiles.clear();
}

bool LabelDictionary::isValidLabelName(const QString &name) const
{
    // Label names should start with a letter or underscore
    // and can contain letters, numbers, and underscores
    if (name.isEmpty()) {
        return false;
    }

    QChar firstChar = name.at(0);
    if (!firstChar.isLetter() && firstChar != '_') {
        return false;
    }

    // Check remaining characters
    for (int i = 1; i < name.length(); ++i) {
        QChar c = name.at(i);
        if (!c.isLetterOrNumber() && c != '_') {
            return false;
        }
    }

    return true;
}

ScriptPosition LabelDictionary::findLabel(const QString &name) const
{
    // Return the script position for the label
    if (m_labels.contains(name)) {
        return ScriptPosition(m_labelFiles.value(name, ""), m_labels.value(name), 0);
    }
    return ScriptPosition("", -1, 0);
}

QStringList LabelDictionary::getAllLabelNames() const
{
    return m_labels.keys();
}

QMap<QString, int> LabelDictionary::getAllLabels() const
{
    // Convert QHash to QMap
    QMap<QString, int> result;
    for (auto it = m_labels.constBegin(); it != m_labels.constEnd(); ++it) {
        result.insert(it.key(), it.value());
    }
    return result;
}

QMap<QString, QString> LabelDictionary::getAllLabelFiles() const
{
    // Convert QHash to QMap
    QMap<QString, QString> result;
    for (auto it = m_labelFiles.constBegin(); it != m_labelFiles.constEnd(); ++it) {
        result.insert(it.key(), it.value());
    }
    return result;
}
