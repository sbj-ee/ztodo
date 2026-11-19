#pragma once

#include <QFont>
#include <QString>

namespace ztodo::appFont {

// Small / Medium / Large relative to the platform default point size.
// Medium is that default (the look before this setting existed). Small is
// 2 points smaller (not below 8 pt). Large is 2 points larger.
enum class Size { Small, Medium, Large };

constexpr qreal kSmallOffset = -2.0;
constexpr qreal kLargeOffset = 2.0;
constexpr qreal kMinPointSize = 8.0;

Size parseSize(const QString &s); // "small" | "medium" | "large"; anything else: Medium
QString sizeToString(Size s);

// Empty or "system" (any case, with surrounding space) means the platform family.
bool isSystemFamily(const QString &family);

// Point size for a named size, given the Medium (platform default) size.
qreal pointSizeFor(Size s, qreal mediumPointSize);

// The platform default font, captured the first time it is needed (after a
// QApplication exists) so later setFont calls don't move the baseline.
QFont systemDefault();

// The application font for a stored family (empty = system) and size.
QFont resolve(const QString &family, Size size);

// Applies the font to the running application. Existing widgets that use the
// default font update immediately; MainWindow also restyles its title and
// list rows on FontChange.
void apply(const QString &family, Size size);

} // namespace ztodo::appFont
