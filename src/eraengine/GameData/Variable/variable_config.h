#ifndef VARIABLE_CONFIG_H
#define VARIABLE_CONFIG_H

#include <QString>
#include <QHash>
#include <QPair>
#include <QVector>
#include <QMap>

struct VariableSizeInfo {
    int size1D = 0;
    QPair<int, int> size2D;
    QVector<int> size3D;
    bool is2D = false;
    bool is3D = false;
};

class VariableConfig
{
private:
    QMap<QString, VariableSizeInfo> variableSizes;
    
public:
    VariableConfig();
    
    bool loadFromCSV(const QString& filePath);
    void loadDefaults();
    
    int getSize1D(const QString& name) const;
    QPair<int, int> getSize2D(const QString& name) const;
    QVector<int> getSize3D(const QString& name) const;
    
    // Get all variable sizes for debugging/inspection
    QMap<QString, VariableSizeInfo> getAllVariableSizes() const { return variableSizes; }
};

#endif // VARIABLE_CONFIG_H