#include "html_export.h"

#include <QRegularExpression>
#include <QStringList>

namespace markpad::markdown {
namespace {

QString escapeText(const QByteArray& text) {
  const QString value = QString::fromUtf8(text);
  QString result;
  result.reserve(value.size());
  for (const auto character : value) {
    if (character == u'&') {
      result += QStringLiteral("&amp;");
    } else if (character == u'<') {
      result += QStringLiteral("&lt;");
    } else if (character == u'>') {
      result += QStringLiteral("&gt;");
    } else if (character == u'"') {
      result += QStringLiteral("&quot;");
    } else if (character == u'\'') {
      result += QStringLiteral("&#39;");
    } else {
      result += character;
    }
  }
  return result;
}

QString escapeAttribute(const QString& value) {
  return escapeText(value.toUtf8());
}

bool safeUrl(const QString& url, bool image) {
  const auto trimmed = url.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }
  for (const auto character : trimmed) {
    if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
      return false;
    }
  }
  static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
  const auto match = scheme.match(trimmed);
  if (!match.hasMatch()) {
    if (image && trimmed.startsWith(QStringLiteral("//"))) {
      return false;
    }
    return true;
  }
  const auto name = match.captured(0).left(match.captured(0).size() - 1).toLower();
  if (name == QStringLiteral("http") || name == QStringLiteral("https")) {
    return true;
  }
  return !image && name == QStringLiteral("mailto");
}

QString alignmentName(int alignment) {
  switch (alignment) {
    case MD_ALIGN_LEFT:
      return QStringLiteral("left");
    case MD_ALIGN_CENTER:
      return QStringLiteral("center");
    case MD_ALIGN_RIGHT:
      return QStringLiteral("right");
    default:
      return {};
  }
}

class HtmlWriter {
 public:
  explicit HtmlWriter(const Document& document) : document_(document) {}

  HtmlRenderResult render() {
    if (document_.nodes.empty()) {
      return {std::nullopt, QStringLiteral("Document has no root node")};
    }
    output_ += QStringLiteral(
        "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
        "<title>Markpad export</title>\n"
        "<style>body{font-family:system-ui,sans-serif;line-height:1.5;"
        "max-width:60rem;margin:2rem auto;padding:0 1rem;}"
        "table{border-collapse:collapse;margin:1rem 0;}"
        "th,td{border:1px solid #999;padding:.35rem .6rem;}"
        "pre{background:#f4f4f4;padding:.75rem;overflow:auto;}"
        "code{font-family:monospace;}blockquote{border-left:4px solid #ccc;"
        "margin-left:0;padding-left:1rem;color:#444;}"
        ".raw-html{white-space:pre-wrap;}</style>\n</head>\n<body>\n");
    renderBlock(0);
    output_ += QStringLiteral("</body>\n</html>\n");
    return {output_, {}};
  }

 private:
  QByteArray plainText(size_t index) const {
    const auto& node = document_.nodes[index];
    if (node.kind == NodeKind::kText &&
        (node.type == MD_TEXT_NORMAL || node.type == MD_TEXT_ENTITY ||
         node.type == MD_TEXT_HTML || node.type == MD_TEXT_CODE)) {
      return node.text;
    }
    QByteArray text;
    for (auto child : node.children) {
      text += plainText(child);
    }
    return text;
  }

  void renderChildren(size_t index) {
    for (auto child : document_.nodes[index].children) {
      renderNode(child);
    }
  }

  void renderText(const Node& node) {
    switch (node.type) {
      case MD_TEXT_BR:
        output_ += QStringLiteral("<br>\n");
        break;
      case MD_TEXT_SOFTBR:
        output_ += QLatin1Char('\n');
        break;
      case MD_TEXT_ENTITY: {
        static const QRegularExpression safeEntity(
            QStringLiteral("^&(#([0-9]+)|#x[0-9A-Fa-f]+|[A-Za-z][A-Za-z0-9]+);$"));
        const auto entity = QString::fromUtf8(node.text);
        output_ += safeEntity.match(entity).hasMatch() ? entity
                                                       : escapeText(node.text);
        break;
      }
      case MD_TEXT_NULLCHAR:
        break;
      default:
        output_ += escapeText(node.text);
        break;
    }
  }

  void renderSpan(size_t index) {
    const auto& node = document_.nodes[index];
    switch (node.type) {
      case MD_SPAN_EM:
        output_ += QStringLiteral("<em>");
        renderChildren(index);
        output_ += QStringLiteral("</em>");
        break;
      case MD_SPAN_STRONG:
        output_ += QStringLiteral("<strong>");
        renderChildren(index);
        output_ += QStringLiteral("</strong>");
        break;
      case MD_SPAN_DEL:
        output_ += QStringLiteral("<del>");
        renderChildren(index);
        output_ += QStringLiteral("</del>");
        break;
      case MD_SPAN_CODE:
        output_ += QStringLiteral("<code>");
        output_ += escapeText(plainText(index));
        output_ += QStringLiteral("</code>");
        break;
      case MD_SPAN_A: {
        const auto href = node.attributes["href"].toString();
        if (!safeUrl(href, false)) {
          renderChildren(index);
          break;
        }
        output_ += QStringLiteral("<a href=\"") + escapeAttribute(href) +
                   QStringLiteral("\"");
        const auto title = node.attributes["title"].toString();
        if (!title.isEmpty()) {
          output_ += QStringLiteral(" title=\"") + escapeAttribute(title) +
                     QStringLiteral("\"");
        }
        output_ += QStringLiteral(" rel=\"noopener noreferrer\">");
        renderChildren(index);
        output_ += QStringLiteral("</a>");
        break;
      }
      case MD_SPAN_IMG: {
        const auto src = node.attributes["src"].toString();
        const auto alt = QString::fromUtf8(plainText(index));
        if (!safeUrl(src, true)) {
          output_ += escapeAttribute(alt);
          break;
        }
        output_ += QStringLiteral("<img src=\"") + escapeAttribute(src) +
                   QStringLiteral("\" alt=\"") + escapeAttribute(alt) +
                   QStringLiteral("\" loading=\"lazy\"");
        const auto title = node.attributes["title"].toString();
        if (!title.isEmpty()) {
          output_ += QStringLiteral(" title=\"") + escapeAttribute(title) +
                     QStringLiteral("\"");
        }
        output_ += QStringLiteral(">");
        break;
      }
      default:
        renderChildren(index);
        break;
    }
  }

  void renderListItem(size_t index) {
    const auto& node = document_.nodes[index];
    output_ += QStringLiteral("<li");
    if (node.attributes["task"].toBool()) {
      output_ += QStringLiteral(" class=\"task-list-item\"");
    }
    output_ += QStringLiteral(">");
    if (node.attributes["task"].toBool()) {
      output_ += node.attributes["checked"].toBool()
                     ? QStringLiteral("<input type=\"checkbox\" checked disabled> ")
                     : QStringLiteral("<input type=\"checkbox\" disabled> ");
    }
    for (auto child : node.children) {
      if (document_.nodes[child].kind == NodeKind::kBlock) {
        renderBlock(child);
      } else {
        renderNode(child);
      }
    }
    output_ += QStringLiteral("</li>\n");
  }

  void renderTable(size_t index) {
    output_ += QStringLiteral("<table>\n");
    renderChildren(index);
    output_ += QStringLiteral("</table>\n");
  }

  void renderBlock(size_t index) {
    const auto& node = document_.nodes[index];
    switch (node.type) {
      case MD_BLOCK_DOC:
        renderChildren(index);
        break;
      case MD_BLOCK_QUOTE:
        output_ += QStringLiteral("<blockquote>\n");
        renderChildren(index);
        output_ += QStringLiteral("</blockquote>\n");
        break;
      case MD_BLOCK_UL:
        output_ += QStringLiteral("<ul>\n");
        for (auto child : node.children) {
          if (document_.nodes[child].type == MD_BLOCK_LI) {
            renderListItem(child);
          }
        }
        output_ += QStringLiteral("</ul>\n");
        break;
      case MD_BLOCK_OL: {
        const auto start = node.attributes["start"].toInt();
        output_ += start == 1 ? QStringLiteral("<ol>\n")
                              : QStringLiteral("<ol start=\"%1\">\n").arg(start);
        for (auto child : node.children) {
          if (document_.nodes[child].type == MD_BLOCK_LI) {
            renderListItem(child);
          }
        }
        output_ += QStringLiteral("</ol>\n");
        break;
      }
      case MD_BLOCK_LI:
        renderListItem(index);
        break;
      case MD_BLOCK_H: {
        const auto level =
            qBound(1, node.attributes["level"].toInt(), 6);
        output_ += QStringLiteral("<h%1>").arg(level);
        renderChildren(index);
        output_ += QStringLiteral("</h%1>\n").arg(level);
        break;
      }
      case MD_BLOCK_P:
        output_ += QStringLiteral("<p>");
        renderChildren(index);
        output_ += QStringLiteral("</p>\n");
        break;
      case MD_BLOCK_CODE:
        output_ += QStringLiteral("<pre><code>");
        output_ += escapeText(plainText(index));
        output_ += QStringLiteral("</code></pre>\n");
        break;
      case MD_BLOCK_HTML:
        output_ += QStringLiteral("<pre class=\"raw-html\">");
        output_ += escapeText(plainText(index));
        output_ += QStringLiteral("</pre>\n");
        break;
      case MD_BLOCK_HR:
        output_ += QStringLiteral("<hr>\n");
        break;
      case MD_BLOCK_TABLE:
        renderTable(index);
        break;
      case MD_BLOCK_THEAD:
        output_ += QStringLiteral("<thead>\n");
        renderChildren(index);
        output_ += QStringLiteral("</thead>\n");
        break;
      case MD_BLOCK_TBODY:
        output_ += QStringLiteral("<tbody>\n");
        renderChildren(index);
        output_ += QStringLiteral("</tbody>\n");
        break;
      case MD_BLOCK_TR:
        output_ += QStringLiteral("<tr>\n");
        renderChildren(index);
        output_ += QStringLiteral("</tr>\n");
        break;
      case MD_BLOCK_TH:
      case MD_BLOCK_TD: {
        const auto tag = node.type == MD_BLOCK_TH ? QStringLiteral("th")
                                                   : QStringLiteral("td");
        output_ += QStringLiteral("<%1").arg(tag);
        const auto alignment =
            alignmentName(node.attributes["align"].toInt());
        if (!alignment.isEmpty()) {
          output_ += QStringLiteral(" align=\"") + alignment +
                     QStringLiteral("\"");
        }
        output_ += QStringLiteral(">");
        renderChildren(index);
        output_ += QStringLiteral("</%1>\n").arg(tag);
        break;
      }
      default:
        renderChildren(index);
        break;
    }
  }

  void renderNode(size_t index) {
    if (index >= document_.nodes.size()) {
      return;
    }
    const auto& node = document_.nodes[index];
    switch (node.kind) {
      case NodeKind::kBlock:
        renderBlock(index);
        break;
      case NodeKind::kSpan:
        renderSpan(index);
        break;
      case NodeKind::kText:
        renderText(node);
        break;
    }
  }

  const Document& document_;
  QString output_;
};

}  // namespace

HtmlRenderResult renderHtml(const Document& document) {
  return HtmlWriter(document).render();
}

}  // namespace markpad::markdown
