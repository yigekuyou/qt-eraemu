#pragma once

#include <QString>

namespace AndroidSafPaths {
QString join(const QString& base, const QString& relative);
QString storageRoot(const QString& game);
}
