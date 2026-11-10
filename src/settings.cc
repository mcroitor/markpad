#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

namespace markpad {
namespace {

constexpr int kSchemaVersion = 1;
constexpr double kMinMargin = 0.0;
constexpr double kMaxMargin = 200.0;
constexpr int kMinFont = 6;
constexpr int kMaxFont = 48;

QString defaultConfigRoot() {
  auto root = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  if (root.isEmpty()) {
    root = QDir::homePath() + QStringLiteral("/.markpad");
  }
  return root;
}

QJsonObject pageObject(const PageSetup& page) {
  return {{"size", SettingsService::pageSizeName(page.size)},
          {"orientation", SettingsService::orientationName(page.orientation)},
          {"marginTopMm", page.marginTopMm},
          {"marginRightMm", page.marginRightMm},
          {"marginBottomMm", page.marginBottomMm},
          {"marginLeftMm", page.marginLeftMm}};
}

double number(const QJsonObject& object, const char* key, double fallback) {
  const auto value = object.value(QLatin1String(key));
  return value.isDouble() ? value.toDouble() : fallback;
}

bool marginValid(double value) {
  return value >= kMinMargin && value <= kMaxMargin;
}

const QMap<QString, QString>& builtinEnglish() {
  static const QMap<QString, QString> values{
      {"menu.file", "&File"},
      {"menu.edit", "&Edit"},
      {"menu.format", "F&ormat"},
      {"menu.view", "&View"},
      {"menu.help", "&Help"},
      {"action.new", "&New"},
      {"action.open", "&Open..."},
      {"action.save", "&Save"},
      {"action.saveAs", "Save &As..."},
      {"action.exportHtml", "Export &HTML..."},
      {"action.preferences", "&Preferences..."},
      {"action.close", "&Close"},
      {"action.exit", "E&xit"},
      {"action.undo", "&Undo"},
      {"action.redo", "&Redo"},
      {"action.cut", "Cu&t"},
      {"action.copy", "&Copy"},
      {"action.paste", "&Paste"},
      {"action.selectAll", "Select &All"},
      {"action.bold", "&Bold"},
      {"action.italic", "&Italic"},
      {"action.strikethrough", "&Strikethrough"},
      {"action.heading", "Heading"},
      {"action.paragraph", "Paragraph"},
      {"action.toggleTask", "Toggle task checked"},
      {"view.switchMode", "Switch mode"},
      {"view.markdown", "Markdown"},
      {"view.continuous", "WYSIWYG (continuous, limited)"},
      {"view.paged", "WYSIWYG (paged, limited)"},
      {"view.statusBar", "Status bar"},
      {"help.about", "About Markpad"},
  };
  return values;
}

}  // namespace

SettingsService::SettingsService(QString configRoot)
    : configRoot_(configRoot.isEmpty() ? defaultConfigRoot()
                                       : std::move(configRoot)) {}

QString SettingsService::configRoot() const {
  return configRoot_;
}

QString SettingsService::configPath() const {
  return QDir(configRoot_).filePath(QStringLiteral("settings.json"));
}

QString SettingsService::languageDirectory() const {
  return QDir(configRoot_).filePath(QStringLiteral("lang"));
}

QString SettingsService::themeName(Theme theme) {
  switch (theme) {
    case Theme::kLight:
      return QStringLiteral("light");
    case Theme::kDark:
      return QStringLiteral("dark");
    case Theme::kSystem:
      break;
  }
  return QStringLiteral("system");
}

Theme SettingsService::parseTheme(const QString& value, Theme fallback) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("light")) {
    return Theme::kLight;
  }
  if (normalized == QStringLiteral("dark")) {
    return Theme::kDark;
  }
  if (normalized == QStringLiteral("system")) {
    return Theme::kSystem;
  }
  return fallback;
}

QString SettingsService::pageSizeName(PageSize size) {
  switch (size) {
    case PageSize::kLetter:
      return QStringLiteral("Letter");
    case PageSize::kLegal:
      return QStringLiteral("Legal");
    case PageSize::kA4:
      break;
  }
  return QStringLiteral("A4");
}

PageSize SettingsService::parsePageSize(const QString& value, PageSize fallback) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("letter")) {
    return PageSize::kLetter;
  }
  if (normalized == QStringLiteral("legal")) {
    return PageSize::kLegal;
  }
  if (normalized == QStringLiteral("a4")) {
    return PageSize::kA4;
  }
  return fallback;
}

QString SettingsService::orientationName(PageOrientation orientation) {
  return orientation == PageOrientation::kLandscape ? QStringLiteral("landscape")
                                                     : QStringLiteral("portrait");
}

PageOrientation SettingsService::parseOrientation(const QString& value,
                                                  PageOrientation fallback) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("portrait")) {
    return PageOrientation::kPortrait;
  }
  if (normalized == QStringLiteral("landscape")) {
    return PageOrientation::kLandscape;
  }
  return fallback;
}

std::optional<QString> SettingsService::validateShortcuts(
    const QMap<QString, QString>& shortcuts) {
  static const QList<QKeySequence::StandardKey> reserved{
      QKeySequence::New,       QKeySequence::Open,      QKeySequence::Save,
      QKeySequence::SaveAs,    QKeySequence::Undo,      QKeySequence::Redo,
      QKeySequence::Cut,       QKeySequence::Copy,      QKeySequence::Paste,
      QKeySequence::SelectAll, QKeySequence::Bold,      QKeySequence::Italic,
      QKeySequence::Close,     QKeySequence::Quit,
  };
  QMap<QString, QString> used;
  for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it) {
    const auto sequence = QKeySequence::fromString(it.value());
    if (sequence.isEmpty() || sequence.count() != 1) {
      return QStringLiteral("Invalid shortcut for %1: %2")
          .arg(it.key(), it.value());
    }
    const auto normalized = sequence.toString(QKeySequence::PortableText);
    if (used.contains(normalized)) {
      return QStringLiteral("Shortcut %1 conflicts with %2")
          .arg(normalized, used.value(normalized));
    }
    for (const auto key : reserved) {
      for (const auto& builtin : QKeySequence::keyBindings(key)) {
        if (normalized ==
            builtin.toString(QKeySequence::PortableText)) {
          return QStringLiteral("Shortcut %1 conflicts with a reserved command")
              .arg(normalized);
        }
      }
    }
    used.insert(normalized, it.key());
  }
  return std::nullopt;
}

SettingsLoadResult SettingsService::load() const {
  SettingsLoadResult result;
  result.settings = Settings{};
  const QFileInfo info(configPath());
  if (!info.exists()) {
    return result;
  }
  result.found = true;
  QFile file(configPath());
  if (!file.open(QIODevice::ReadOnly)) {
    result.diagnostic =
        QStringLiteral("Cannot read settings file; using defaults: %1")
            .arg(file.errorString());
    return result;
  }
  QJsonParseError parseError{};
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError ||
      !document.isObject()) {
    result.diagnostic =
        QStringLiteral("Invalid settings file; using defaults");
    return result;
  }
  const auto object = document.object();
  const auto version = object.value(QStringLiteral("schemaVersion")).toInt(0);
  if (version > kSchemaVersion) {
    result.diagnostic =
        QStringLiteral("Unsupported settings schema; using defaults");
    return result;
  }
  auto& settings = result.settings;
  settings.schemaVersion = kSchemaVersion;
  if (object.contains(QStringLiteral("language"))) {
    settings.language = object.value(QStringLiteral("language")).toString();
  }
  if (settings.language.trimmed().isEmpty()) {
    settings.language = QStringLiteral("en");
    result.diagnostic += QStringLiteral("Invalid language; using English. ");
  } else if (!languages().contains(settings.language)) {
    result.diagnostic +=
        QStringLiteral("Unsupported language '%1'; using English. ")
            .arg(settings.language);
    settings.language = QStringLiteral("en");
  }
  if (object.contains(QStringLiteral("theme"))) {
    const auto value = object.value(QStringLiteral("theme")).toString();
    settings.theme = parseTheme(value, settings.theme);
    if (themeName(settings.theme) != value.trimmed().toLower()) {
      result.diagnostic += QStringLiteral("Invalid theme; using default. ");
    }
  }
  settings.editorFontFamily =
      object.value(QStringLiteral("editorFontFamily")).toString();
  const auto fontSize = object.value(QStringLiteral("editorFontSize")).toInt(
      settings.editorFontSize);
  if (fontSize < kMinFont || fontSize > kMaxFont) {
    result.diagnostic += QStringLiteral("Invalid editor font size; using 12. ");
  } else {
    settings.editorFontSize = fontSize;
  }
  settings.wordWrap = object.value(QStringLiteral("wordWrap"))
                          .toBool(settings.wordWrap);
  settings.defaultFileLocation =
      object.value(QStringLiteral("defaultFileLocation")).toString();
  if (object.value(QStringLiteral("page")).isObject()) {
    const auto page = object.value(QStringLiteral("page")).toObject();
    settings.page.size = parsePageSize(
        page.value(QStringLiteral("size")).toString(), settings.page.size);
    settings.page.orientation = parseOrientation(
        page.value(QStringLiteral("orientation")).toString(),
        settings.page.orientation);
    const auto margins = {number(page, "marginTopMm", settings.page.marginTopMm),
                          number(page, "marginRightMm",
                                 settings.page.marginRightMm),
                          number(page, "marginBottomMm",
                                 settings.page.marginBottomMm),
                          number(page, "marginLeftMm",
                                 settings.page.marginLeftMm)};
    if (!std::all_of(margins.begin(), margins.end(), marginValid)) {
      result.diagnostic += QStringLiteral("Invalid page margins; using 20 mm. ");
      settings.page.marginTopMm = settings.page.marginRightMm =
          settings.page.marginBottomMm = settings.page.marginLeftMm = 20.0;
    } else {
      settings.page.marginTopMm =
          number(page, "marginTopMm", settings.page.marginTopMm);
      settings.page.marginRightMm =
          number(page, "marginRightMm", settings.page.marginRightMm);
      settings.page.marginBottomMm =
          number(page, "marginBottomMm", settings.page.marginBottomMm);
      settings.page.marginLeftMm =
          number(page, "marginLeftMm", settings.page.marginLeftMm);
    }
  }
  if (object.value(QStringLiteral("shortcuts")).isObject()) {
    const auto shortcuts =
        object.value(QStringLiteral("shortcuts")).toObject();
    for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it) {
      if (it.value().isString()) {
        settings.shortcuts.insert(it.key(), it.value().toString());
      }
    }
    if (const auto error = validateShortcuts(settings.shortcuts)) {
      result.diagnostic += *error + QStringLiteral("; shortcuts ignored. ");
      settings.shortcuts.clear();
    }
  }
  return result;
}

std::optional<QString> SettingsService::save(const Settings& settings) const {
  if (!QDir().mkpath(configRoot_)) {
    return QStringLiteral("Cannot create settings directory: %1")
        .arg(configRoot_);
  }
  QJsonObject page = pageObject(settings.page);
  QJsonObject shortcuts;
  for (auto it = settings.shortcuts.begin(); it != settings.shortcuts.end();
       ++it) {
    shortcuts.insert(it.key(), it.value());
  }
  const QJsonObject object{
      {"schemaVersion", kSchemaVersion},
      {"language", settings.language},
      {"theme", themeName(settings.theme)},
      {"editorFontFamily", settings.editorFontFamily},
      {"editorFontSize", settings.editorFontSize},
      {"wordWrap", settings.wordWrap},
      {"defaultFileLocation", settings.defaultFileLocation},
      {"page", page},
      {"shortcuts", shortcuts},
  };
  QSaveFile file(configPath());
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    return QStringLiteral("Cannot write settings file: %1")
        .arg(file.errorString());
  }
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
  if (file.write(bytes) != bytes.size() || !file.commit()) {
    const auto error = QStringLiteral("Cannot commit settings file: %1")
                           .arg(file.errorString());
    file.cancelWriting();
    return error;
  }
  return std::nullopt;
}

QStringList SettingsService::languages() const {
  QStringList result{QStringLiteral("en")};
  QDir directory(languageDirectory());
  if (directory.exists()) {
    for (const auto& entry :
         directory.entryList({QStringLiteral("*.ini")}, QDir::Files)) {
      const auto name = QFileInfo(entry).completeBaseName();
      if (!name.isEmpty() && !result.contains(name)) {
        result.push_back(name);
      }
    }
  }
  result.sort(Qt::CaseInsensitive);
  return result;
}

QMap<QString, QString> SettingsService::languageMap(
    const QString& language) const {
  QMap<QString, QString> result = builtinEnglish();
  const auto path =
      QDir(languageDirectory()).filePath(language + QStringLiteral(".ini"));
  QSettings file(path, QSettings::IniFormat);
  for (const auto& key : file.allKeys()) {
    result.insert(key, file.value(key).toString());
  }
  return result;
}

}  // namespace markpad
