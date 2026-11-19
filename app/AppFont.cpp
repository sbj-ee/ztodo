#include "AppFont.hpp"

#include <QApplication>
#include <QFontInfo>

#include <algorithm>

namespace ztodo::appFont {

namespace {
struct Original {
    bool captured = false;
    QFont font;
};

Original &original()
{
    static Original o;
    return o;
}

void capture()
{
    Original &o = original();
    if (!o.captured) {
        o.captured = true;
        o.font = QApplication::font();
    }
}

qreal effectivePointSize(const QFont &f)
{
    if (f.pointSizeF() > 0) {
        return f.pointSizeF();
    }
    const qreal info = QFontInfo(f).pointSizeF();
    return info > 0 ? info : 10.0;
}
} // namespace

Size parseSize(const QString &s)
{
    const QString v = s.trimmed().toLower();
    return v == QLatin1String("small") ? Size::Small
         : v == QLatin1String("large") ? Size::Large
                                       : Size::Medium;
}

QString sizeToString(Size s)
{
    return s == Size::Small ? QStringLiteral("small")
         : s == Size::Large ? QStringLiteral("large")
                            : QStringLiteral("medium");
}

bool isSystemFamily(const QString &family)
{
    const QString v = family.trimmed();
    return v.isEmpty() || v.compare(QLatin1String("system"), Qt::CaseInsensitive) == 0;
}

qreal pointSizeFor(Size s, qreal mediumPointSize)
{
    const qreal mid = mediumPointSize > 0 ? mediumPointSize : 10.0;
    if (s == Size::Small) {
        return std::max(kMinPointSize, mid + kSmallOffset);
    }
    if (s == Size::Large) {
        return mid + kLargeOffset;
    }
    return mid;
}

QFont systemDefault()
{
    capture();
    return original().font;
}

QFont resolve(const QString &family, Size size)
{
    QFont f = systemDefault();
    if (!isSystemFamily(family)) {
        f.setFamily(family);
    }
    f.setPointSizeF(pointSizeFor(size, effectivePointSize(systemDefault())));
    return f;
}

void apply(const QString &family, Size size)
{
    capture();
    QApplication::setFont(resolve(family, size));
}

} // namespace ztodo::appFont
