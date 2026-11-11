#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "settings.h"

namespace markpad {
namespace {

void writeFile(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(file.write(bytes), bytes.size());
}

}  // namespace

class SettingsTest : public QObject {
  Q_OBJECT

 private slots:
  void defaults() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SettingsService service(directory.path());
    const auto loaded = service.load();
    QVERIFY(!loaded.found);
    QVERIFY(loaded.diagnostic.isEmpty());
    QCOMPARE(loaded.settings.language, QStringLiteral("en"));
    QCOMPARE(loaded.settings.theme, Theme::kSystem);
    QCOMPARE(loaded.settings.editorFontSize, 12);
    QVERIFY(loaded.settings.wordWrap);
    QCOMPARE(loaded.settings.page.size, PageSize::kA4);
    QCOMPARE(loaded.settings.page.orientation, PageOrientation::kPortrait);
    QCOMPARE(loaded.settings.page.marginTopMm, 20.0);
    QCOMPARE(loaded.settings.page.marginRightMm, 20.0);
    QCOMPARE(loaded.settings.page.marginBottomMm, 20.0);
    QCOMPARE(loaded.settings.page.marginLeftMm, 20.0);
  }

  void roundTrip() {
    QTemporaryDir directory;
    SettingsService service(directory.path());
    Settings settings;
    settings.language = QStringLiteral("en");
    settings.theme = Theme::kDark;
    settings.editorFontFamily = QStringLiteral("Cascadia Mono");
    settings.editorFontSize = 15;
    settings.wordWrap = false;
    settings.defaultFileLocation = QStringLiteral("/tmp");
    settings.page.size = PageSize::kLetter;
    settings.page.orientation = PageOrientation::kLandscape;
    settings.page.marginTopMm = 12.5;
    settings.page.marginRightMm = 13.5;
    settings.page.marginBottomMm = 14.5;
    settings.page.marginLeftMm = 15.5;
    settings.shortcuts.insert(QStringLiteral("boldAction"),
                              QStringLiteral("Ctrl+Alt+B"));
    QVERIFY2(!service.save(settings), "settings save failed");
    const auto loaded = service.load();
    QVERIFY(loaded.found);
    QVERIFY(loaded.diagnostic.isEmpty());
    QCOMPARE(loaded.settings.theme, Theme::kDark);
    QCOMPARE(loaded.settings.editorFontFamily, QStringLiteral("Cascadia Mono"));
    QCOMPARE(loaded.settings.editorFontSize, 15);
    QVERIFY(!loaded.settings.wordWrap);
    QCOMPARE(loaded.settings.defaultFileLocation, QStringLiteral("/tmp"));
    QCOMPARE(loaded.settings.page.size, PageSize::kLetter);
    QCOMPARE(loaded.settings.page.orientation, PageOrientation::kLandscape);
    QCOMPARE(loaded.settings.page.marginTopMm, 12.5);
    QCOMPARE(loaded.settings.page.marginLeftMm, 15.5);
    QCOMPARE(loaded.settings.shortcuts.value(QStringLiteral("boldAction")),
             QStringLiteral("Ctrl+Alt+B"));
  }

  void corruptAndUnsupportedSettingsFallBack() {
    QTemporaryDir directory;
    SettingsService service(directory.path());
    QVERIFY(QDir().mkpath(directory.path()));
    writeFile(service.configPath(), "{not json");
    auto loaded = service.load();
    QVERIFY(loaded.found);
    QVERIFY(!loaded.diagnostic.isEmpty());
    QCOMPARE(loaded.settings.theme, Theme::kSystem);
    QCOMPARE(loaded.settings.editorFontSize, 12);

    writeFile(service.configPath(),
              R"({"schemaVersion":99,"theme":"dark"})");
    loaded = service.load();
    QVERIFY(loaded.found);
    QVERIFY(loaded.diagnostic.contains(QStringLiteral("schema")));
    QCOMPARE(loaded.settings.theme, Theme::kSystem);
  }

  void invalidFieldsUseSafeFallbacks() {
    QTemporaryDir directory;
    SettingsService service(directory.path());
    writeFile(service.configPath(),
              R"({
                "schemaVersion":1,
                "language":"missing",
                "theme":"neon",
                "editorFontSize":99,
                "page":{"size":"B5","orientation":"sideways",
                        "marginTopMm":-1,"marginRightMm":20,
                        "marginBottomMm":20,"marginLeftMm":20}
              })");
    const auto loaded = service.load();
    QVERIFY(loaded.found);
    QVERIFY(!loaded.diagnostic.isEmpty());
    QCOMPARE(loaded.settings.language, QStringLiteral("en"));
    QCOMPARE(loaded.settings.theme, Theme::kSystem);
    QCOMPARE(loaded.settings.editorFontSize, 12);
    QCOMPARE(loaded.settings.page.size, PageSize::kA4);
    QCOMPARE(loaded.settings.page.orientation, PageOrientation::kPortrait);
    QCOMPARE(loaded.settings.page.marginTopMm, 20.0);
  }

  void shortcutValidation() {
    QVERIFY(!SettingsService::validateShortcuts(
        {{QStringLiteral("boldAction"), QStringLiteral("Ctrl+Alt+K")}}));
    QVERIFY(SettingsService::validateShortcuts(
        {{QStringLiteral("boldAction"), QString()}}));
    QVERIFY(SettingsService::validateShortcuts(
        {{QStringLiteral("a"), QStringLiteral("Ctrl+Alt+K")},
         {QStringLiteral("b"), QStringLiteral("Ctrl+Alt+K")}}));
    QVERIFY(SettingsService::validateShortcuts(
        {{QStringLiteral("saveAction"), QStringLiteral("Ctrl+S")}}));
  }

  void languageFilesAndFallback() {
    QTemporaryDir directory;
    SettingsService service(directory.path());
    QVERIFY(QDir().mkpath(service.languageDirectory()));
    writeFile(QDir(service.languageDirectory()).filePath(QStringLiteral("ru.ini")),
              "menu.file=Файл\naction.new=Новый\n");
    const auto languages = service.languages();
    QVERIFY(languages.contains(QStringLiteral("en")));
    QVERIFY(languages.contains(QStringLiteral("ru")));
    const auto russian = service.languageMap(QStringLiteral("ru"));
    QCOMPARE(russian.value(QStringLiteral("menu.file")), QStringLiteral("Файл"));
    QCOMPARE(russian.value(QStringLiteral("action.new")), QStringLiteral("Новый"));
    QCOMPARE(russian.value(QStringLiteral("action.save")), QStringLiteral("&Save"));
    const auto missing = service.languageMap(QStringLiteral("missing"));
    QCOMPARE(missing.value(QStringLiteral("menu.file")), QStringLiteral("&File"));
  }
};

}  // namespace markpad

QTEST_MAIN(markpad::SettingsTest)

#include "settings_test.moc"
