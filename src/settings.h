#ifndef MARKPAD_SRC_SETTINGS_H_
#define MARKPAD_SRC_SETTINGS_H_

#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>

namespace markpad {

enum class Theme { kSystem, kLight, kDark };
enum class PageSize { kA4, kLetter, kLegal };
enum class PageOrientation { kPortrait, kLandscape };

struct PageSetup {
  PageSize size = PageSize::kA4;
  PageOrientation orientation = PageOrientation::kPortrait;
  double marginTopMm = 20.0;
  double marginRightMm = 20.0;
  double marginBottomMm = 20.0;
  double marginLeftMm = 20.0;
};

struct Settings {
  int schemaVersion = 1;
  QString language = QStringLiteral("en");
  Theme theme = Theme::kSystem;
  QString editorFontFamily;
  int editorFontSize = 12;
  bool wordWrap = true;
  QString defaultFileLocation;
  PageSetup page;
  QMap<QString, QString> shortcuts;
};

struct SettingsLoadResult {
  Settings settings;
  QString diagnostic;
  bool found = false;
};

// User settings are stored in the platform AppConfigLocation for Markpad.
// Tests may pass an explicit root directory.
class SettingsService final {
 public:
  explicit SettingsService(QString configRoot = {});

  QString configRoot() const;
  QString configPath() const;
  QString languageDirectory() const;

  SettingsLoadResult load() const;
  std::optional<QString> save(const Settings& settings) const;

  QStringList languages() const;
  QMap<QString, QString> languageMap(const QString& language) const;

  // Returns an error when one or more shortcuts are invalid, duplicate or
  // conflict with reserved editor/application shortcuts.
  static std::optional<QString> validateShortcuts(
      const QMap<QString, QString>& shortcuts);

  static QString themeName(Theme theme);
  static Theme parseTheme(const QString& value, Theme fallback);
  static QString pageSizeName(PageSize size);
  static PageSize parsePageSize(const QString& value, PageSize fallback);
  static QString orientationName(PageOrientation orientation);
  static PageOrientation parseOrientation(const QString& value,
                                          PageOrientation fallback);

 private:
  QString configRoot_;
};

}  // namespace markpad

#endif  // MARKPAD_SRC_SETTINGS_H_
