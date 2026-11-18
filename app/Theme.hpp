#pragma once

#include <QApplication>
#include <QColor>
#include <QPalette>

namespace ztodo::theme {

// The logo's green, and Todoist-like priority colours.
inline QColor accent() { return QColor(0x2f, 0xb3, 0x4a); }
inline QColor priority(int p)
{
    switch (p) {
    case 1:
        return QColor(0xd1, 0x45, 0x3b);
    case 2:
        return QColor(0xeb, 0x89, 0x09);
    case 3:
        return QColor(0x24, 0x6f, 0xe0);
    default:
        return QColor(0x8a, 0x8f, 0x98);
    }
}
inline bool isDark()
{
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}
// Due-date colours: lighter on a dark background so they stay readable there,
// including on the selected row.
inline QColor overdue() { return isDark() ? QColor(0xf0, 0x7a, 0x70) : QColor(0xd1, 0x45, 0x3b); }
inline QColor today() { return isDark() ? QColor(0x5c, 0xd6, 0x76) : QColor(0x15, 0x9a, 0x3c); }
inline QColor tomorrow() { return isDark() ? QColor(0xf2, 0xa8, 0x3c) : QColor(0xc0, 0x74, 0x00); }
inline QColor thisWeek() { return isDark() ? QColor(0xb7, 0x8c, 0xf0) : QColor(0x8a, 0x4f, 0xd8); }
inline QColor muted()
{
    return isDark() ? QColor(0x9a, 0xa0, 0xa6) : QColor(0x70, 0x75, 0x7c);
}
inline QColor separator()
{
    return isDark() ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 22);
}
inline QColor hover()
{
    return isDark() ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 10);
}

// Sidebar: a clearly different background from the task pane, and
// secondary text (section headers, counts) that still reads well on it.
inline QColor sidebarBackground()
{
    const QColor w = QApplication::palette().color(QPalette::Active, QPalette::Window);
    return isDark() ? w.darker(145) : w.darker(109);
}
inline QColor sidebarMuted()
{
    return isDark() ? QColor(0xc6, 0xcb, 0xd1) : QColor(0x3d, 0x42, 0x49);
}
inline QColor sidebarBorder()
{
    return isDark() ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 45);
}
// The default label colour (Store::addLabel) is drawn in sidebarMuted().
inline bool isDefaultLabelColor(const QColor &c) { return c == QColor(0x8a, 0x8f, 0x98); }

// Project colours offered for new projects, in turn.
inline QColor projectColor(int i)
{
    static const QRgb c[] = {0x2fb34a, 0x246fe0, 0xeb8909, 0xd1453b, 0x8a4fd8, 0x14aaf5, 0xe05194, 0x6accbc, 0xafb83b, 0x808080};
    return QColor(c[(i % 10 + 10) % 10]);
}

} // namespace ztodo::theme
