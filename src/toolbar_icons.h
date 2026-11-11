#ifndef MARKPAD_SRC_TOOLBAR_ICONS_H_
#define MARKPAD_SRC_TOOLBAR_ICONS_H_

#include <QColor>
#include <QIcon>
#include <QString>

namespace markpad {

// Monochrome, theme-colored toolbar icon for a known action object name.
QIcon toolbarIcon(const QString& actionName, const QColor& color);

}  // namespace markpad

#endif  // MARKPAD_SRC_TOOLBAR_ICONS_H_
