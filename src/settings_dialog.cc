#include "settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace markpad {

SettingsDialog::SettingsDialog(const QStringList& languages, QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(QStringLiteral("Preferences"));
  auto* layout = new QVBoxLayout(this);

  auto* general = new QGroupBox(QStringLiteral("Application"), this);
  auto* generalForm = new QFormLayout(general);
  theme_ = new QComboBox(general);
  theme_->addItem(QStringLiteral("System"), int(Theme::kSystem));
  theme_->addItem(QStringLiteral("Light"), int(Theme::kLight));
  theme_->addItem(QStringLiteral("Dark"), int(Theme::kDark));
  generalForm->addRow(QStringLiteral("Theme"), theme_);
  language_ = new QComboBox(general);
  language_->addItems(languages);
  generalForm->addRow(QStringLiteral("Language"), language_);
  defaultLocation_ = new QLineEdit(general);
  generalForm->addRow(QStringLiteral("Default file location"),
                      defaultLocation_);
  layout->addWidget(general);

  auto* editor = new QGroupBox(QStringLiteral("Editor"), this);
  auto* editorForm = new QFormLayout(editor);
  font_ = new QFontComboBox(editor);
  editorForm->addRow(QStringLiteral("Font"), font_);
  fontSize_ = new QSpinBox(editor);
  fontSize_->setRange(6, 48);
  editorForm->addRow(QStringLiteral("Font size"), fontSize_);
  wordWrap_ = new QCheckBox(QStringLiteral("Wrap long lines"), editor);
  editorForm->addRow(wordWrap_);
  layout->addWidget(editor);

  auto* page = new QGroupBox(QStringLiteral("Page"), this);
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
  layout->addWidget(page);

  auto* shortcuts = new QGroupBox(QStringLiteral("Shortcuts"), this);
  auto* shortcutsLayout = new QVBoxLayout(shortcuts);
  shortcuts_ = new QPlainTextEdit(shortcuts);
  shortcuts_->setPlaceholderText(
      QStringLiteral("One override per line: actionName=Ctrl+Alt+K"));
  shortcutsLayout->addWidget(shortcuts_);
  layout->addWidget(shortcuts);

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
