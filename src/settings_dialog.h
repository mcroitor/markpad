#ifndef MARKPAD_SRC_SETTINGS_DIALOG_H_
#define MARKPAD_SRC_SETTINGS_DIALOG_H_

#include <QDialog>
#include <QMap>
#include <QStringList>
#include <array>
#include <optional>

#include "settings.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace markpad {

class SettingsDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit SettingsDialog(const QStringList& languages,
                          QWidget* parent = nullptr);
  void setValues(const Settings& settings);
  Settings values() const;
  std::optional<QString> validationError() const;

 protected:
  void accept() override;

 private:
  Settings collect() const;
  std::optional<QMap<QString, QString>> parseShortcuts(
      QString* error) const;

  QComboBox* theme_;
  QComboBox* language_;
  QFontComboBox* font_;
  QSpinBox* fontSize_;
  QCheckBox* wordWrap_;
  QLineEdit* defaultLocation_;
  QComboBox* pageSize_;
  QComboBox* orientation_;
  QDoubleSpinBox* marginTop_;
  QDoubleSpinBox* marginRight_;
  QDoubleSpinBox* marginBottom_;
  QDoubleSpinBox* marginLeft_;
  QFontComboBox* wysiwygBodyFont_;
  QSpinBox* wysiwygBodySize_;
  QFontComboBox* wysiwygHeadingFont_;
  QCheckBox* wysiwygHeadingBold_;
  std::array<QSpinBox*, 6> wysiwygHeadingSizes_{};
  QFontComboBox* wysiwygCodeFont_;
  QSpinBox* wysiwygCodeSize_;
  QFontComboBox* wysiwygQuoteFont_;
  QSpinBox* wysiwygQuoteSize_;
  QCheckBox* wysiwygQuoteItalic_;
  QPlainTextEdit* shortcuts_;
};

}  // namespace markpad

#endif  // MARKPAD_SRC_SETTINGS_DIALOG_H_
