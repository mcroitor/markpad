#include "settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
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
  auto* pageForm = new QFormLayout(page);
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
  tabs->addTab(page, QStringLiteral("Page"));

  auto* shortcutPage = new QWidget(tabs);
  shortcutPage->setObjectName(QStringLiteral("settingsShortcutTab"));
  auto* shortcutsLayout = new QVBoxLayout(shortcutPage);
  shortcuts_ = new QPlainTextEdit(shortcutPage);
  shortcuts_->setPlaceholderText(
      QStringLiteral("One override per line: actionName=Ctrl+Alt+K"));
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
  shortcuts_->setObjectName(QStringLiteral("settingsShortcuts"));
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
