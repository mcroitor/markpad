#include "settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace markpad {

SettingsDialog::SettingsDialog(const QStringList& languages, QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(QStringLiteral("Settings"));
  auto* layout = new QVBoxLayout(this);
  auto* tabs = new QTabWidget(this);
  tabs->setObjectName(QStringLiteral("settingsTabs"));

  auto* generalPage = new QWidget(tabs);
  generalPage->setObjectName(QStringLiteral("settingsGeneralTab"));
  auto* generalForm = new QFormLayout(generalPage);
  language_ = new QComboBox(generalPage);
  language_->addItems(languages);
  generalForm->addRow(QStringLiteral("Language"), language_);
  theme_ = new QComboBox(generalPage);
  theme_->addItem(QStringLiteral("System"), int(Theme::kSystem));
  theme_->addItem(QStringLiteral("Light"), int(Theme::kLight));
  theme_->addItem(QStringLiteral("Dark"), int(Theme::kDark));
  generalForm->addRow(QStringLiteral("Theme"), theme_);
  auto* autoSave = new QCheckBox(
      QStringLiteral("Auto Save (deferred)"), generalPage);
  autoSave->setObjectName(QStringLiteral("settingsAutoSave"));
  autoSave->setEnabled(false);
  autoSave->setToolTip(
      QStringLiteral("Auto Save is deferred until a recovery policy is "
                     "approved"));
  generalForm->addRow(autoSave);
  defaultLocation_ = new QLineEdit(generalPage);
  generalForm->addRow(QStringLiteral("Default file location"),
                      defaultLocation_);
  tabs->addTab(generalPage, QStringLiteral("General"));

  auto* editorPage = new QWidget(tabs);
  editorPage->setObjectName(QStringLiteral("settingsEditorTab"));
  auto* editorForm = new QFormLayout(editorPage);
  fontSize_ = new QSpinBox(editorPage);
  fontSize_->setRange(6, 48);
  editorForm->addRow(QStringLiteral("Font size"), fontSize_);
  font_ = new QFontComboBox(editorPage);
  editorForm->addRow(QStringLiteral("Font family"), font_);
  wordWrap_ = new QCheckBox(QStringLiteral("Wrap long lines"), editorPage);
  editorForm->addRow(wordWrap_);
  auto* lineNumbers = new QCheckBox(
      QStringLiteral("Line numbers (deferred)"), editorPage);
  lineNumbers->setObjectName(QStringLiteral("settingsLineNumbers"));
  lineNumbers->setEnabled(false);
  lineNumbers->setToolTip(
      QStringLiteral("Line-number gutter is deferred to a later settings "
                     "slice"));
  editorForm->addRow(lineNumbers);
  tabs->addTab(editorPage, QStringLiteral("Editor"));

  auto* page = new QWidget(tabs);
  page->setObjectName(QStringLiteral("settingsPageTab"));
  auto* pageLayout = new QVBoxLayout(page);
  auto* pageForm = new QFormLayout();
  pageSize_ = new QComboBox(page);
  pageSize_->addItem(QStringLiteral("A4"), int(PageSize::kA4));
  pageSize_->addItem(QStringLiteral("Letter"), int(PageSize::kLetter));
  pageSize_->addItem(QStringLiteral("Legal"), int(PageSize::kLegal));
  pageForm->addRow(QStringLiteral("Page size"), pageSize_);
  orientation_ = new QComboBox(page);
  orientation_->addItem(QStringLiteral("Portrait"),
                        int(PageOrientation::kPortrait));
  orientation_->addItem(QStringLiteral("Landscape"),
                        int(PageOrientation::kLandscape));
  pageForm->addRow(QStringLiteral("Orientation"), orientation_);
  const auto createMargin = [page]() {
    auto* spin = new QDoubleSpinBox(page);
    spin->setRange(0.0, 200.0);
    spin->setDecimals(1);
    spin->setSuffix(QStringLiteral(" mm"));
    return spin;
  };
  marginTop_ = createMargin();
  marginRight_ = createMargin();
  marginBottom_ = createMargin();
  marginLeft_ = createMargin();
  pageForm->addRow(QStringLiteral("Top margin"), marginTop_);
  pageForm->addRow(QStringLiteral("Right margin"), marginRight_);
  pageForm->addRow(QStringLiteral("Bottom margin"), marginBottom_);
  pageForm->addRow(QStringLiteral("Left margin"), marginLeft_);
  pageLayout->addLayout(pageForm);

  auto* typography = new QGroupBox(
      QStringLiteral("WYSIWYG typography"), page);
  auto* typographyForm = new QFormLayout(typography);
  wysiwygBodyFont_ = new QFontComboBox(typography);
  typographyForm->addRow(QStringLiteral("Body font"), wysiwygBodyFont_);
  wysiwygBodySize_ = new QSpinBox(typography);
  wysiwygBodySize_->setRange(6, 72);
  typographyForm->addRow(QStringLiteral("Body size"), wysiwygBodySize_);
  wysiwygHeadingFont_ = new QFontComboBox(typography);
  typographyForm->addRow(QStringLiteral("Heading font"),
                         wysiwygHeadingFont_);
  wysiwygHeadingBold_ =
      new QCheckBox(QStringLiteral("Bold headings"), typography);
  typographyForm->addRow(wysiwygHeadingBold_);
  auto* headingSizes = new QWidget(typography);
  auto* headingLayout = new QHBoxLayout(headingSizes);
  headingLayout->setContentsMargins(0, 0, 0, 0);
  for (int level = 0; level < 6; ++level) {
    headingLayout->addWidget(
        new QLabel(QStringLiteral("H%1").arg(level + 1), headingSizes));
    wysiwygHeadingSizes_[size_t(level)] = new QSpinBox(headingSizes);
    wysiwygHeadingSizes_[size_t(level)]->setRange(6, 96);
    headingLayout->addWidget(wysiwygHeadingSizes_[size_t(level)]);
  }
  typographyForm->addRow(QStringLiteral("Heading sizes"), headingSizes);
  wysiwygCodeFont_ = new QFontComboBox(typography);
  typographyForm->addRow(QStringLiteral("Code font"), wysiwygCodeFont_);
  wysiwygCodeSize_ = new QSpinBox(typography);
  wysiwygCodeSize_->setRange(6, 72);
  typographyForm->addRow(QStringLiteral("Code size"), wysiwygCodeSize_);
  wysiwygQuoteFont_ = new QFontComboBox(typography);
  typographyForm->addRow(QStringLiteral("Quote font"), wysiwygQuoteFont_);
  wysiwygQuoteSize_ = new QSpinBox(typography);
  wysiwygQuoteSize_->setRange(6, 72);
  typographyForm->addRow(QStringLiteral("Quote size"), wysiwygQuoteSize_);
  wysiwygQuoteItalic_ =
      new QCheckBox(QStringLiteral("Italic quotes"), typography);
  typographyForm->addRow(wysiwygQuoteItalic_);
  pageLayout->addWidget(typography);
  pageLayout->addStretch();
  tabs->addTab(page, QStringLiteral("Page"));

  auto* shortcutPage = new QWidget(tabs);
  shortcutPage->setObjectName(QStringLiteral("settingsShortcutTab"));
  auto* shortcutsLayout = new QVBoxLayout(shortcutPage);
  auto* catalogHint = new QLabel(
      QStringLiteral("Available actions and their default shortcuts:"),
      shortcutPage);
  shortcutsLayout->addWidget(catalogHint);
  shortcutCatalog_ = new QTableWidget(0, 3, shortcutPage);
  shortcutCatalog_->setObjectName(QStringLiteral("settingsShortcutCatalog"));
  shortcutCatalog_->setHorizontalHeaderLabels(
      {QStringLiteral("Action"), QStringLiteral("Default"),
       QStringLiteral("Object name")});
  shortcutCatalog_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  shortcutCatalog_->setSelectionBehavior(QAbstractItemView::SelectRows);
  shortcutCatalog_->setSelectionMode(QAbstractItemView::SingleSelection);
  shortcutCatalog_->horizontalHeader()->setStretchLastSection(true);
  shortcutCatalog_->verticalHeader()->setVisible(false);
  shortcutCatalog_->setMinimumHeight(180);
  shortcutsLayout->addWidget(shortcutCatalog_);
  shortcutsLayout->addWidget(new QLabel(
      QStringLiteral("Overrides (one per line, actionName=Shortcut):"),
      shortcutPage));
  shortcuts_ = new QPlainTextEdit(shortcutPage);
  shortcuts_->setPlaceholderText(
      QStringLiteral("One override per line: actionName=Ctrl+Alt+K"));
  shortcuts_->setMaximumHeight(120);
  shortcutsLayout->addWidget(shortcuts_);
  tabs->addTab(shortcutPage, QStringLiteral("Shortcut"));

  layout->addWidget(tabs);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
  setObjectName(QStringLiteral("settingsDialog"));
  theme_->setObjectName(QStringLiteral("settingsTheme"));
  language_->setObjectName(QStringLiteral("settingsLanguage"));
  font_->setObjectName(QStringLiteral("settingsFont"));
  fontSize_->setObjectName(QStringLiteral("settingsFontSize"));
  wordWrap_->setObjectName(QStringLiteral("settingsWordWrap"));
  defaultLocation_->setObjectName(QStringLiteral("settingsDefaultLocation"));
  pageSize_->setObjectName(QStringLiteral("settingsPageSize"));
  orientation_->setObjectName(QStringLiteral("settingsOrientation"));
  marginTop_->setObjectName(QStringLiteral("settingsMarginTop"));
  marginRight_->setObjectName(QStringLiteral("settingsMarginRight"));
  marginBottom_->setObjectName(QStringLiteral("settingsMarginBottom"));
  marginLeft_->setObjectName(QStringLiteral("settingsMarginLeft"));
  wysiwygBodyFont_->setObjectName(QStringLiteral("settingsWysiwygBodyFont"));
  wysiwygBodySize_->setObjectName(QStringLiteral("settingsWysiwygBodySize"));
  wysiwygHeadingFont_->setObjectName(
      QStringLiteral("settingsWysiwygHeadingFont"));
  wysiwygHeadingBold_->setObjectName(
      QStringLiteral("settingsWysiwygHeadingBold"));
  for (int level = 0; level < 6; ++level) {
    wysiwygHeadingSizes_[size_t(level)]->setObjectName(
        QStringLiteral("settingsWysiwygHeading%1Size").arg(level + 1));
  }
  wysiwygCodeFont_->setObjectName(QStringLiteral("settingsWysiwygCodeFont"));
  wysiwygCodeSize_->setObjectName(QStringLiteral("settingsWysiwygCodeSize"));
  wysiwygQuoteFont_->setObjectName(QStringLiteral("settingsWysiwygQuoteFont"));
  wysiwygQuoteSize_->setObjectName(QStringLiteral("settingsWysiwygQuoteSize"));
  wysiwygQuoteItalic_->setObjectName(
      QStringLiteral("settingsWysiwygQuoteItalic"));
  shortcuts_->setObjectName(QStringLiteral("settingsShortcuts"));
}

void SettingsDialog::setShortcutCatalog(const QList<ShortcutInfo>& catalog) {
  shortcutCatalog_->setRowCount(catalog.size());
  for (int row = 0; row < catalog.size(); ++row) {
    const auto& info = catalog[row];
    auto label = info.label;
    label.remove(QChar('&'));
    shortcutCatalog_->setItem(row, 0,
                              new QTableWidgetItem(label.trimmed()));
    shortcutCatalog_->setItem(
        row, 1,
        new QTableWidgetItem(info.defaultShortcut.isEmpty()
                                 ? QStringLiteral("(none)")
                                 : info.defaultShortcut));
    shortcutCatalog_->setItem(row, 2,
                              new QTableWidgetItem(info.objectName));
  }
  shortcutCatalog_->resizeColumnsToContents();
}

void SettingsDialog::setValues(const Settings& settings) {
  theme_->setCurrentIndex(theme_->findData(int(settings.theme)));
  if (theme_->currentIndex() < 0) {
    theme_->setCurrentIndex(0);
  }
  const auto language = language_->findText(settings.language);
  language_->setCurrentIndex(language >= 0 ? language : 0);
  font_->setCurrentFont(QFont(settings.editorFontFamily));
  fontSize_->setValue(settings.editorFontSize);
  wordWrap_->setChecked(settings.wordWrap);
  defaultLocation_->setText(settings.defaultFileLocation);
  pageSize_->setCurrentIndex(pageSize_->findData(int(settings.page.size)));
  orientation_->setCurrentIndex(
      orientation_->findData(int(settings.page.orientation)));
  marginTop_->setValue(settings.page.marginTopMm);
  marginRight_->setValue(settings.page.marginRightMm);
  marginBottom_->setValue(settings.page.marginBottomMm);
  marginLeft_->setValue(settings.page.marginLeftMm);
  const auto bodyFamily = settings.wysiwyg.bodyFontFamily.trimmed().isEmpty()
                              ? settings.editorFontFamily
                              : settings.wysiwyg.bodyFontFamily;
  wysiwygBodyFont_->setCurrentFont(QFont(bodyFamily));
  wysiwygBodySize_->setValue(settings.wysiwyg.bodyFontSize);
  wysiwygHeadingFont_->setCurrentFont(QFont(
      settings.wysiwyg.headingFontFamily.trimmed().isEmpty()
          ? bodyFamily
          : settings.wysiwyg.headingFontFamily));
  wysiwygHeadingBold_->setChecked(settings.wysiwyg.headingBold);
  for (int level = 0; level < 6; ++level) {
    wysiwygHeadingSizes_[size_t(level)]->setValue(
        settings.wysiwyg.headingSizes[size_t(level)]);
  }
  wysiwygCodeFont_->setCurrentFont(
      settings.wysiwyg.codeFontFamily.trimmed().isEmpty()
          ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
          : QFont(settings.wysiwyg.codeFontFamily));
  wysiwygCodeSize_->setValue(settings.wysiwyg.codeFontSize);
  wysiwygQuoteFont_->setCurrentFont(QFont(
      settings.wysiwyg.quoteFontFamily.trimmed().isEmpty()
          ? bodyFamily
          : settings.wysiwyg.quoteFontFamily));
  wysiwygQuoteSize_->setValue(settings.wysiwyg.quoteFontSize);
  wysiwygQuoteItalic_->setChecked(settings.wysiwyg.quoteItalic);
  QStringList lines;
  for (auto it = settings.shortcuts.begin(); it != settings.shortcuts.end();
       ++it) {
    lines.push_back(it.key() + QStringLiteral("=") + it.value());
  }
  shortcuts_->setPlainText(lines.join(QStringLiteral("\n")));
}

std::optional<QMap<QString, QString>> SettingsDialog::parseShortcuts(
    QString* error) const {
  QMap<QString, QString> result;
  const auto lines =
      shortcuts_->toPlainText().split(QChar('\n'), Qt::SkipEmptyParts);
  for (const auto& raw : lines) {
    const auto line = raw.trimmed();
    if (line.isEmpty() || line.startsWith(QChar('#'))) {
      continue;
    }
    const auto separator = line.indexOf(QChar('='));
    if (separator <= 0 || separator + 1 >= line.size()) {
      if (error) {
        *error = QStringLiteral("Invalid shortcut line: %1").arg(raw);
      }
      return std::nullopt;
    }
    const auto action = line.left(separator).trimmed();
    const auto sequence = line.mid(separator + 1).trimmed();
    if (action.isEmpty() || sequence.isEmpty()) {
      if (error) {
        *error = QStringLiteral("Invalid shortcut line: %1").arg(raw);
      }
      return std::nullopt;
    }
    result.insert(action, sequence);
  }
  if (const auto validation = SettingsService::validateShortcuts(result)) {
    if (error) {
      *error = *validation;
    }
    return std::nullopt;
  }
  return result;
}

Settings SettingsDialog::collect() const {
  Settings settings;
  settings.theme = static_cast<Theme>(theme_->currentData().toInt());
  settings.language = language_->currentText();
  settings.editorFontFamily = font_->currentFont().family();
  settings.editorFontSize = fontSize_->value();
  settings.wordWrap = wordWrap_->isChecked();
  settings.defaultFileLocation = defaultLocation_->text().trimmed();
  settings.page.size =
      static_cast<PageSize>(pageSize_->currentData().toInt());
  settings.page.orientation =
      static_cast<PageOrientation>(orientation_->currentData().toInt());
  settings.page.marginTopMm = marginTop_->value();
  settings.page.marginRightMm = marginRight_->value();
  settings.page.marginBottomMm = marginBottom_->value();
  settings.page.marginLeftMm = marginLeft_->value();
  settings.wysiwyg.bodyFontFamily = wysiwygBodyFont_->currentFont().family();
  settings.wysiwyg.bodyFontSize = wysiwygBodySize_->value();
  settings.wysiwyg.headingFontFamily =
      wysiwygHeadingFont_->currentFont().family();
  settings.wysiwyg.headingBold = wysiwygHeadingBold_->isChecked();
  for (int level = 0; level < 6; ++level) {
    settings.wysiwyg.headingSizes[size_t(level)] =
        wysiwygHeadingSizes_[size_t(level)]->value();
  }
  settings.wysiwyg.codeFontFamily = wysiwygCodeFont_->currentFont().family();
  settings.wysiwyg.codeFontSize = wysiwygCodeSize_->value();
  settings.wysiwyg.quoteFontFamily = wysiwygQuoteFont_->currentFont().family();
  settings.wysiwyg.quoteFontSize = wysiwygQuoteSize_->value();
  settings.wysiwyg.quoteItalic = wysiwygQuoteItalic_->isChecked();
  if (const auto shortcuts = parseShortcuts(nullptr)) {
    settings.shortcuts = *shortcuts;
  }
  return settings;
}

Settings SettingsDialog::values() const {
  return collect();
}

std::optional<QString> SettingsDialog::validationError() const {
  QString error;
  if (!parseShortcuts(&error)) {
    return error;
  }
  if (language_->currentText().trimmed().isEmpty()) {
    return QStringLiteral("Language must not be empty");
  }
  return std::nullopt;
}

void SettingsDialog::accept() {
  if (const auto error = validationError()) {
    QMessageBox::warning(this, QStringLiteral("Preferences"), *error);
    return;
  }
  QDialog::accept();
}

}  // namespace markpad
