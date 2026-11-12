// SPDX-License-Identifier: GPL-3.0-or-later
//
// KnobFunctions: the settings keys behind the Mirabox N3 knob choices. Keys must be unique and
// round-trip, and an empty or unknown stored key must fall back to the knob's default rather than
// silently switching the knob off.

#include "utils/knobfunction.h"

#include <QSet>
#include <QtTest/QtTest>

class TestKnobFunction : public QObject {
    Q_OBJECT

private slots:
    void keysAreUniqueAndRoundTrip() {
        QSet<QString> keys;
        for (const KnobFunctions::Entry &e : KnobFunctions::all()) {
            const QString key = QString::fromLatin1(e.key);
            QVERIFY2(!keys.contains(key), qPrintable(key));
            keys.insert(key);
            QCOMPARE(KnobFunctions::fromKey(key, KnobFunction::VfoA), e.function);
            QCOMPARE(KnobFunctions::key(e.function), key);
        }
    }

    void unsetOrUnknownKeyUsesTheDefault() {
        QCOMPARE(KnobFunctions::fromKey(QString(), KnobFunction::RitXit), KnobFunction::RitXit);
        QCOMPARE(KnobFunctions::fromKey(QStringLiteral("noSuchKnob"), KnobFunction::VfoB), KnobFunction::VfoB);
    }

    void offIsStoredExplicitly() {
        QCOMPARE(KnobFunctions::fromKey(QStringLiteral("none"), KnobFunction::VfoA), KnobFunction::None);
    }
};

QTEST_APPLESS_MAIN(TestKnobFunction)
#include "test_knobfunction.moc"
