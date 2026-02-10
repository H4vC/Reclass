#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include "themes/theme.h"
#include "themes/thememanager.h"

using namespace rcx;

class TestTheme : public QObject {
    Q_OBJECT
private slots:
    void builtInThemes() {
        Theme dark = Theme::reclassDark();
        QCOMPARE(dark.name, "Reclass Dark");
        QVERIFY(dark.background.isValid());
        QVERIFY(dark.text.isValid());
        QVERIFY(dark.syntaxKeyword.isValid());
        QVERIFY(dark.markerError.isValid());

        Theme warm = Theme::warm();
        QCOMPARE(warm.name, "Warm");
        QVERIFY(warm.background.isValid());
        QVERIFY(warm.text.isValid());
        QCOMPARE(warm.background, QColor("#212121"));
        QCOMPARE(warm.selection, QColor("#21213A"));
        QCOMPARE(warm.syntaxKeyword, QColor("#AA9565"));
        QCOMPARE(warm.syntaxType, QColor("#6B959F"));
    }

    void selectionColorFixed() {
        Theme dark = Theme::reclassDark();
        QCOMPARE(dark.selection, QColor("#2b2b2b"));
        QVERIFY(dark.selection != QColor("#264f78"));
    }

    void jsonRoundTrip() {
        Theme orig = Theme::reclassDark();
        QJsonObject json = orig.toJson();
        Theme loaded = Theme::fromJson(json);

        QCOMPARE(loaded.name, orig.name);
        QCOMPARE(loaded.background, orig.background);
        QCOMPARE(loaded.text, orig.text);
        QCOMPARE(loaded.selection, orig.selection);
        QCOMPARE(loaded.syntaxKeyword, orig.syntaxKeyword);
        QCOMPARE(loaded.syntaxNumber, orig.syntaxNumber);
        QCOMPARE(loaded.syntaxString, orig.syntaxString);
        QCOMPARE(loaded.syntaxComment, orig.syntaxComment);
        QCOMPARE(loaded.syntaxType, orig.syntaxType);
        QCOMPARE(loaded.markerPtr, orig.markerPtr);
        QCOMPARE(loaded.markerError, orig.markerError);
        QCOMPARE(loaded.indHoverSpan, orig.indHoverSpan);
    }

    void jsonRoundTripWarm() {
        Theme orig = Theme::warm();
        QJsonObject json = orig.toJson();
        Theme loaded = Theme::fromJson(json);

        QCOMPARE(loaded.name, orig.name);
        QCOMPARE(loaded.background, orig.background);
        QCOMPARE(loaded.selection, orig.selection);
        QCOMPARE(loaded.syntaxKeyword, orig.syntaxKeyword);
    }

    void fromJsonMissingFields() {
        QJsonObject sparse;
        sparse["name"] = "Sparse";
        sparse["background"] = "#ff0000";
        Theme t = Theme::fromJson(sparse);

        QCOMPARE(t.name, "Sparse");
        QCOMPARE(t.background, QColor("#ff0000"));
        // Missing fields fall back to reclassDark defaults
        Theme defaults = Theme::reclassDark();
        QCOMPARE(t.text, defaults.text);
        QCOMPARE(t.syntaxKeyword, defaults.syntaxKeyword);
        QCOMPARE(t.markerError, defaults.markerError);
    }

    void themeManagerHasBuiltIns() {
        auto& tm = ThemeManager::instance();
        auto all = tm.themes();
        QVERIFY(all.size() >= 2);
        QCOMPARE(all[0].name, "Reclass Dark");
        QCOMPARE(all[1].name, "Warm");
    }

    void themeManagerSwitch() {
        auto& tm = ThemeManager::instance();
        QSignalSpy spy(&tm, &ThemeManager::themeChanged);

        int startIdx = tm.currentIndex();
        int target = (startIdx == 0) ? 1 : 0;
        tm.setCurrent(target);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(tm.currentIndex(), target);
        QCOMPARE(tm.current().name, tm.themes()[target].name);

        // Restore
        tm.setCurrent(startIdx);
    }

    void themeManagerCRUD() {
        auto& tm = ThemeManager::instance();
        int initialCount = tm.themes().size();

        // Add
        Theme custom = Theme::reclassDark();
        custom.name = "Test Custom";
        custom.background = QColor("#ff0000");
        tm.addTheme(custom);
        QCOMPARE(tm.themes().size(), initialCount + 1);
        QCOMPARE(tm.themes().last().name, "Test Custom");

        // Update
        int idx = tm.themes().size() - 1;
        Theme updated = custom;
        updated.background = QColor("#00ff00");
        tm.updateTheme(idx, updated);
        QCOMPARE(tm.themes()[idx].background, QColor("#00ff00"));

        // Remove
        tm.removeTheme(idx);
        QCOMPARE(tm.themes().size(), initialCount);
    }
};

QTEST_MAIN(TestTheme)
#include "test_theme.moc"
