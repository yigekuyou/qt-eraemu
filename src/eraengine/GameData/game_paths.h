#pragma once
#include <QString>

// SAF document IDs are opaque. Resolve existing children by enumerating their
// display names, retaining the URI returned by the provider.
namespace GamePaths {
bool isContent(const QString& path);
QString normalize(const QString& path);
QString join(const QString& base, const QString& relative);
QString storageRoot(const QString& game);
}
