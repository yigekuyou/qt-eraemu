/*
 * Regression for GraphicsStore::measureText in a QCoreApplication-only host.
 */
#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <limits>

#include "GameView/graphics_store.h"

static int failures = 0;
static void check(bool ok, const QString& message) {
    if (ok) qDebug().noquote() << "[PASS]" << message;
    else { qCritical().noquote() << "[FAIL]" << message; ++failures; }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QSize normal = GraphicsStore::measureText(QStringLiteral("abc"), {}, 18, 0);
    check(normal.width() > 0 && normal.height() == 18,
          QStringLiteral("QCoreApplication fallback returns positive dimensions"));

    const QSize negative = GraphicsStore::measureText(QStringLiteral("abc"), {}, -100, 0);
    check(negative.width() > 0 && negative.height() == 1,
          QStringLiteral("negative font size is clamped"));

    const QSize extreme = GraphicsStore::measureText(QStringLiteral("x"), {},
                                                       std::numeric_limits<int>::max(), 0);
    check(extreme.width() > 0 && extreme.width() <= std::numeric_limits<int>::max()
              && extreme.height() == std::numeric_limits<int>::max(),
          QStringLiteral("extreme font size stays within QSize int limits"));

    const QString longText(100000, QLatin1Char('x'));
    const QSize longMeasured = GraphicsStore::measureText(longText, {},
                                                            std::numeric_limits<int>::max(), 0);
    check(longMeasured.width() == std::numeric_limits<int>::max(),
          QStringLiteral("large text measurement saturates instead of overflowing"));

    return failures == 0 ? 0 : 1;
}
