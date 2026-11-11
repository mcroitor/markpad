#include "toolbar_icons.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QRectF>
#include <QStringList>

namespace markpad {
namespace {

constexpr int kIconSize = 24;

void textGlyph(QPainter& painter, const QString& text, const QColor& color,
               int pointSize, bool bold = false, bool italic = false) {
  painter.save();
  QFont font = painter.font();
  font.setPointSize(pointSize);
  font.setBold(bold);
  font.setItalic(italic);
  painter.setFont(font);
  painter.setPen(color);
  painter.drawText(QRectF(0, 0, kIconSize, kIconSize), Qt::AlignCenter, text);
  painter.restore();
}

void drawUndoArrow(QPainter& painter, const QColor& color) {
  QPainterPath path;
  path.moveTo(18, 18);
  path.cubicTo(18, 10, 10, 7, 6, 12);
  painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);
  painter.drawLine(QPointF(6, 12), QPointF(9, 9));
  painter.drawLine(QPointF(6, 12), QPointF(9, 15));
}

void drawIcon(QPainter& painter, const QString& name, const QColor& color) {
  const QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  if (name == QStringLiteral("newAction")) {
    painter.drawRoundedRect(QRectF(4, 3, 12, 18), 1.5, 1.5);
    painter.drawLine(QPointF(16, 14), QPointF(21, 14));
    painter.drawLine(QPointF(18.5, 11.5), QPointF(18.5, 16.5));
  } else if (name == QStringLiteral("openAction")) {
    painter.drawPath([] {
      QPainterPath path;
      path.moveTo(3, 8);
      path.lineTo(9, 8);
      path.lineTo(11, 10);
      path.lineTo(21, 10);
      path.lineTo(21, 19);
      path.lineTo(3, 19);
      path.closeSubpath();
      return path;
    }());
  } else if (name == QStringLiteral("saveAction")) {
    painter.drawRoundedRect(QRectF(4, 3, 16, 18), 1.5, 1.5);
    painter.drawRect(QRectF(8, 3, 8, 5));
    painter.drawRect(QRectF(7, 13, 10, 8));
  } else if (name == QStringLiteral("undoAction")) {
    drawUndoArrow(painter, color);
  } else if (name == QStringLiteral("redoAction")) {
    painter.save();
    painter.translate(kIconSize, 0);
    painter.scale(-1, 1);
    drawUndoArrow(painter, color);
    painter.restore();
  } else if (name == QStringLiteral("settingsAction")) {
    for (int row = 0; row < 3; ++row) {
      const qreal y = 7 + row * 5;
      painter.drawLine(QPointF(4, y), QPointF(20, y));
    }
    painter.setBrush(color);
    painter.drawEllipse(QPointF(9, 7), 2, 2);
    painter.drawEllipse(QPointF(15, 12), 2, 2);
    painter.drawEllipse(QPointF(8, 17), 2, 2);
  } else if (name == QStringLiteral("headingMenuAction")) {
    textGlyph(painter, QStringLiteral("H"), color, 15, true);
  } else if (name == QStringLiteral("boldAction")) {
    textGlyph(painter, QStringLiteral("B"), color, 15, true);
  } else if (name == QStringLiteral("italicAction")) {
    textGlyph(painter, QStringLiteral("I"), color, 15, false, true);
  } else if (name == QStringLiteral("strikethroughAction")) {
    textGlyph(painter, QStringLiteral("S"), color, 15);
    painter.setPen(pen);
    painter.drawLine(QPointF(4, 13), QPointF(20, 13));
  } else if (name == QStringLiteral("blockquoteAction")) {
    painter.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
    painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(8, 7), QPointF(8, 17));
    painter.setPen(pen);
    painter.drawLine(QPointF(11, 8), QPointF(18, 8));
    painter.drawLine(QPointF(11, 12), QPointF(18, 12));
    painter.drawLine(QPointF(11, 16), QPointF(16, 16));
  } else if (name == QStringLiteral("codeAction")) {
    painter.drawPolyline(
        QPolygonF{QPointF(9, 7), QPointF(4, 12), QPointF(9, 17)});
    painter.drawPolyline(
        QPolygonF{QPointF(15, 7), QPointF(20, 12), QPointF(15, 17)});
    painter.drawLine(QPointF(13, 5), QPointF(11, 19));
  } else if (name == QStringLiteral("horizontalLineAction")) {
    painter.drawLine(QPointF(3, 12), QPointF(21, 12));
    painter.drawLine(QPointF(6, 8), QPointF(6, 16));
    painter.drawLine(QPointF(18, 8), QPointF(18, 16));
  } else if (name == QStringLiteral("linkAction")) {
    painter.drawRoundedRect(QRectF(3, 9, 10, 6), 3, 3);
    painter.drawRoundedRect(QRectF(11, 9, 10, 6), 3, 3);
    painter.drawLine(QPointF(8, 12), QPointF(16, 12));
  } else if (name == QStringLiteral("imageAction")) {
    painter.drawRoundedRect(QRectF(3, 5, 18, 14), 1.5, 1.5);
    painter.drawEllipse(QPointF(8, 9), 2, 2);
    painter.drawPolyline(QPolygonF{QPointF(5, 17), QPointF(10, 11),
                                   QPointF(13, 14), QPointF(16, 10),
                                   QPointF(20, 16)});
  } else if (name == QStringLiteral("tableAction")) {
    painter.drawRect(QRectF(3, 5, 18, 14));
    painter.drawLine(QPointF(9, 5), QPointF(9, 19));
    painter.drawLine(QPointF(15, 5), QPointF(15, 19));
    painter.drawLine(QPointF(3, 9), QPointF(21, 9));
    painter.drawLine(QPointF(3, 15), QPointF(21, 15));
  } else if (name == QStringLiteral("listMenuAction") ||
             name == QStringLiteral("unorderedListAction")) {
    painter.setBrush(color);
    for (int row = 0; row < 3; ++row) {
      const qreal y = 7 + row * 5;
      painter.drawEllipse(QPointF(5, y), 1.4, 1.4);
    }
    painter.setPen(pen);
    painter.drawLine(QPointF(9, 7), QPointF(20, 7));
    painter.drawLine(QPointF(9, 12), QPointF(20, 12));
    painter.drawLine(QPointF(9, 17), QPointF(20, 17));
  } else if (name == QStringLiteral("orderedListAction")) {
    painter.save();
    QFont font = painter.font();
    font.setPointSize(9);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QRectF(1, 2, 9, 20), Qt::AlignCenter,
                     QStringLiteral("1"));
    painter.restore();
    painter.setPen(pen);
    painter.drawLine(QPointF(11, 7), QPointF(20, 7));
    painter.drawLine(QPointF(11, 12), QPointF(20, 12));
    painter.drawLine(QPointF(11, 17), QPointF(20, 17));
  } else if (name == QStringLiteral("switchModeAction")) {
    painter.drawRoundedRect(QRectF(3, 5, 12, 15), 1.5, 1.5);
    painter.drawRoundedRect(QRectF(9, 3, 12, 15), 1.5, 1.5);
    painter.drawLine(QPointF(12, 11), QPointF(18, 11));
    painter.drawLine(QPointF(12, 16), QPointF(18, 16));
  }
}

QPixmap makePixmap(const QString& name, const QColor& color) {
  QPixmap pixmap(kIconSize, kIconSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  drawIcon(painter, name, color);
  return pixmap;
}

}  // namespace

QIcon toolbarIcon(const QString& actionName, const QColor& color) {
  const QStringList supported{
      QStringLiteral("newAction"),
      QStringLiteral("openAction"),
      QStringLiteral("saveAction"),
      QStringLiteral("undoAction"),
      QStringLiteral("redoAction"),
      QStringLiteral("settingsAction"),
      QStringLiteral("headingMenuAction"),
      QStringLiteral("boldAction"),
      QStringLiteral("italicAction"),
      QStringLiteral("strikethroughAction"),
      QStringLiteral("blockquoteAction"),
      QStringLiteral("codeAction"),
      QStringLiteral("horizontalLineAction"),
      QStringLiteral("linkAction"),
      QStringLiteral("imageAction"),
      QStringLiteral("tableAction"),
      QStringLiteral("listMenuAction"),
      QStringLiteral("orderedListAction"),
      QStringLiteral("unorderedListAction"),
      QStringLiteral("switchModeAction"),
  };
  if (!supported.contains(actionName)) {
    return {};
  }
  QIcon icon;
  icon.addPixmap(makePixmap(actionName, color), QIcon::Normal);
  auto disabled = color;
  disabled.setAlphaF(0.35);
  icon.addPixmap(makePixmap(actionName, disabled), QIcon::Disabled);
  return icon;
}

}  // namespace markpad
