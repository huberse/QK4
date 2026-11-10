// SPDX-License-Identifier: GPL-3.0-or-later
//
// MiraboxN3Protocol: the input decoder (key, small button, knob push and knob turn codes) and the
// output reports (command framing, brightness, image header and chunking). Pure logic, no device.

#include "hardware/miraboxn3protocol.h"

#include <QtTest/QtTest>

namespace P = MiraboxN3Protocol;

namespace {
// An input report as hid_read returns it: "ACK" 0 0 "OK" 0 0 <code> <state>, zero-padded.
QByteArray input(quint8 code, quint8 state) {
    QByteArray r(512, '\0');
    r.replace(0, 7, QByteArray("ACK\0\0OK", 7));
    r[9] = static_cast<char>(code);
    r[10] = static_cast<char>(state);
    return r;
}

P::InputEvent decode(const QByteArray &r) {
    return P::decodeInput(reinterpret_cast<const unsigned char *>(r.constData()), int(r.size()));
}
} // namespace

class TestMiraboxN3Protocol : public QObject {
    Q_OBJECT

private slots:
    void lcdKeys() {
        for (quint8 key = 1; key <= 6; ++key) {
            const P::InputEvent press = decode(input(key, 1));
            QCOMPARE(press.type, P::InputEvent::Type::Button);
            QCOMPARE(press.index, int(key));
            QVERIFY(press.pressed);
            QVERIFY(!decode(input(key, 0)).pressed);
        }
    }

    void smallButtonsAndKnobPushes() {
        const QList<QPair<quint8, int>> map = {{0x25, 7}, {0x30, 8}, {0x31, 9}, {0x35, 10}, {0x33, 11}, {0x34, 12}};
        for (const auto &[code, button] : map) {
            const P::InputEvent ev = decode(input(code, 1));
            QCOMPARE(ev.type, P::InputEvent::Type::Button);
            QCOMPARE(ev.index, button);
        }
    }

    void knobs() {
        const QList<QPair<quint8, int>> base = {{0x50, 1}, {0x90, 2}, {0x60, 3}};
        for (const auto &[code, knob] : base) {
            const P::InputEvent ccw = decode(input(code, 0));
            QCOMPARE(ccw.type, P::InputEvent::Type::Knob);
            QCOMPARE(ccw.index, knob);
            QCOMPARE(ccw.ticks, -1);
            const P::InputEvent cw = decode(input(quint8(code | 1), 0));
            QCOMPARE(cw.index, knob);
            QCOMPARE(cw.ticks, 1);
        }
    }

    void nothingAndUnknown() {
        QCOMPARE(decode(input(0, 0)).type, P::InputEvent::Type::None);
        QCOMPARE(decode(QByteArray(5, '\0')).type, P::InputEvent::Type::None);
        const P::InputEvent unknown = decode(input(0x77, 1));
        QCOMPARE(unknown.type, P::InputEvent::Type::Unknown);
        QCOMPARE(unknown.rawCode, quint8(0x77));
    }

    void commandFraming() {
        const QByteArray r = P::wakeScreen();
        QCOMPARE(r.size(), P::kReportPayload + 1);
        QCOMPARE(r[0], '\0'); // report ID
        QCOMPARE(r.mid(1, 8), QByteArray("CRT\0\0DIS", 8));
        QCOMPARE(P::flush().mid(6, 3), QByteArray("STP"));
        QCOMPARE(P::clearKey(0xFF).mid(6, 3), QByteArray("CLE"));
        QCOMPARE(quint8(P::clearKey(0xFF)[12]), quint8(0xFF));
    }

    void brightnessIsClamped() {
        QCOMPARE(int(P::brightness(50)[11]), 50);
        QCOMPARE(int(P::brightness(150)[11]), 100);
        QCOMPARE(int(P::brightness(-3)[11]), 0);
    }

    void imageHeaderAndChunks() {
        const QByteArray h = P::imageHeader(4, 0x01020304);
        QCOMPARE(h.mid(6, 3), QByteArray("BAT"));
        QCOMPARE(h.mid(9, 4), QByteArray("\x01\x02\x03\x04", 4));
        QCOMPARE(int(h[13]), 4);

        QByteArray jpeg(2500, 'x');
        jpeg[0] = 'a';
        jpeg[2499] = 'z';
        const QList<QByteArray> chunks = P::imageChunks(jpeg);
        QCOMPARE(chunks.size(), 3);
        for (const QByteArray &c : chunks) {
            QCOMPARE(c.size(), P::kReportPayload + 1);
            QCOMPARE(c[0], '\0');
        }
        QCOMPARE(chunks[0][1], 'a');
        QCOMPARE(chunks[2][1 + 2499 - 2048], 'z');
        QCOMPARE(chunks[2][1 + 2500 - 2048], '\0'); // padded
        QVERIFY(P::imageChunks(QByteArray()).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestMiraboxN3Protocol)
#include "test_miraboxn3protocol.moc"
