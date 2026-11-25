#include "Appearance.hpp"

#include <QApplication>
#include <QStyle>

#include <cstdlib>

namespace ztodo::appearance {

namespace {
struct Original {
    bool captured = false;
    QString style;
    QPalette palette;
    bool forced = false; // a forced theme is currently applied
};

Original &original()
{
    static Original o;
    return o;
}

QPalette makePalette(const QColor &window, const QColor &base, const QColor &alt, const QColor &text,
                     const QColor &button, const QColor &muted, const QColor &accent, const QColor &link)
{
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, link);
    p.setColor(QPalette::LinkVisited, link);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, muted);
    }
    return p;
}
} // namespace

Mode parse(const QString &s)
{
    const QString v = s.trimmed().toLower();
    return v == QLatin1String("light") ? Mode::Light : v == QLatin1String("dark") ? Mode::Dark : Mode::System;
}

QString toString(Mode m)
{
    return m == Mode::Light ? QStringLiteral("light") : m == Mode::Dark ? QStringLiteral("dark") : QStringLiteral("system");
}

QPalette lightPalette()
{
    return makePalette(QColor(0xf3, 0xf4, 0xf6), Qt::white, QColor(0xf7, 0xf8, 0xf9), QColor(0x1d, 0x1f, 0x23),
                       QColor(0xe6, 0xe8, 0xeb), QColor(0x70, 0x75, 0x7c), QColor(0x2f, 0xb3, 0x4a),
                       QColor(0x1a, 0x6f, 0xd0));
}

QPalette darkPalette()
{
    return makePalette(QColor(0x2b, 0x2d, 0x31), QColor(0x1f, 0x21, 0x24), QColor(0x25, 0x27, 0x2b),
                       QColor(0xe6, 0xe8, 0xeb), QColor(0x38, 0x3b, 0x40), QColor(0x9a, 0xa0, 0xa6),
                       QColor(0x2a, 0x9d, 0x42), QColor(0x6c, 0xb0, 0xff));
}

void styleDialogs()
{
    if (QApplication::style()->name().compare(QLatin1String("macos"), Qt::CaseInsensitive) == 0) {
        return;
    }
    QPalette p = QApplication::palette();
    const QColor window = p.color(QPalette::Active, QPalette::Window);
    QColor bg;
    QColor button = p.color(QPalette::Active, QPalette::Button);
    if (window.lightness() < 128) {
        bg = window.lighter(140);
        // Buttons were only a little lighter than the window: keep them
        // apart from the lighter dialog too.
        if (std::abs(button.lightness() - bg.lightness()) < 16) {
            button = bg.lighter(130);
        }
    } else {
        // A light window is already close to white, so a white dialog would
        // barely differ from it: a slightly darker grey reads more clearly.
        bg = window.darker(110);
        if (std::abs(button.lightness() - bg.lightness()) < 16) {
            button = bg.darker(112);
        }
    }
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::Button, button);
    if (QApplication::palette("QDialog") != p) {
        QApplication::setPalette(p, "QDialog");
    }
}

void apply(Mode m)
{
    Original &o = original();
    if (!o.captured) {
        o.captured = true;
        o.style = QApplication::style()->objectName();
        o.palette = QApplication::palette();
    }
    if (m == Mode::System) {
        if (o.forced) {
            o.forced = false;
            QApplication::setStyle(o.style);
            QApplication::setPalette(o.palette);
        }
        styleDialogs();
        return;
    }
    o.forced = true;
    // Fusion honours the palette fully on every platform; native styles may not.
    if (QApplication::style()->objectName().compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0) {
        QApplication::setStyle(QStringLiteral("Fusion"));
    }
    QApplication::setPalette(m == Mode::Dark ? darkPalette() : lightPalette());
    styleDialogs();
}

} // namespace ztodo::appearance
