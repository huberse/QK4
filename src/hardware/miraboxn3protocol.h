#ifndef MIRABOXN3PROTOCOL_H
#define MIRABOXN3PROTOCOL_H

#include <QByteArray>
#include <QList>
#include <QtGlobal>
#include <cstring>

// Wire protocol of the Mirabox N3 stream controller (6 LCD keys, 3 small buttons, 3 push knobs).
//
// Header-only and Qt Core only, so the decoder and the command builders are unit-tested without a
// device (tests/test_miraboxn3protocol.cpp). All hidapi I/O lives in MiraboxN3Worker.
//
// WHERE THIS COMES FROM: Mirabox publishes no protocol document. The layout below follows wfview's
// usbcontroller.cpp (GPL-3.0, as is QK4), which drives the same device, and is confirmed against a
// real N3 only as far as that code is.
//
// Every report QK4 writes starts with a report-ID byte of 0 (the N3 uses unnumbered reports); hidapi
// strips it before the wire on every platform. Commands are "CRT" 0 0 <3-letter verb> <args...>,
// zero-padded to the full output report.
namespace MiraboxN3Protocol {

constexpr quint16 kVendorId = 0x6603;
constexpr quint16 kProductId = 0x1003;
// The device exposes more than one HID interface; this is the vendor-defined one that carries keys.
constexpr quint16 kUsagePage = 0xFFA0;

// Output report payload, without the report-ID byte.
constexpr int kReportPayload = 1024;
// Edge length of a key image in pixels. Images go over the wire as JPEG, rotated 90 degrees.
constexpr int kKeyImageSize = 72;

constexpr int kLcdKeys = 6;  // keys 1..6, the only ones with a display
constexpr int kButtons = 12; // 6 LCD keys + 3 small buttons + 3 knob pushes
constexpr int kKnobs = 3;

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

struct InputEvent {
    enum class Type { None, Button, Knob, Unknown };
    Type type = Type::None;
    // Button: 1..12 — 1..6 LCD keys (row-major from top left), 7..9 the small buttons below them,
    // 10..12 pushing knob 1..3. Knob: 1..3.
    int index = 0;
    bool pressed = false; // Button: true on press, false on release
    int ticks = 0;        // Knob: +1 clockwise, -1 counter-clockwise
    quint8 rawCode = 0;   // the code byte as received, for logging unknown input
};

/// Decode one input report as returned by hid_read (no report-ID byte; "ACK" 0 0 "OK" ... at the
/// start, the key code at offset 9 and its state at offset 10).
inline InputEvent decodeInput(const unsigned char *data, int length) {
    InputEvent ev;
    if (!data || length < 11)
        return ev;
    const quint8 code = data[9];
    const bool state = (data[10] & 0x01) != 0;
    ev.rawCode = code;
    if (code == 0)
        return ev;

    // Knobs: the high nibble names the knob, bit 0 the direction.
    const quint8 hi = code >> 4;
    if (hi == 0x5 || hi == 0x9 || hi == 0x6) {
        ev.type = InputEvent::Type::Knob;
        ev.index = (hi == 0x5) ? 1 : (hi == 0x9) ? 2 : 3;
        ev.ticks = (code & 0x01) ? 1 : -1;
        return ev;
    }

    int button = 0;
    if (code >= 1 && code <= kLcdKeys) {
        button = code;
    } else {
        switch (code) {
        case 0x25:
            button = 7;
            break;
        case 0x30:
            button = 8;
            break;
        case 0x31:
            button = 9;
            break;
        case 0x35:
            button = 10; // knob 1 push
            break;
        case 0x33:
            button = 11; // knob 2 push
            break;
        case 0x34:
            button = 12; // knob 3 push
            break;
        default:
            break;
        }
    }
    if (button == 0) {
        ev.type = InputEvent::Type::Unknown;
        return ev;
    }
    ev.type = InputEvent::Type::Button;
    ev.index = button;
    ev.pressed = state;
    return ev;
}

// ---------------------------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------------------------

/// A zeroed report (report-ID byte + payload) carrying "CRT" 0 0 <verb>.
inline QByteArray command(const char verb[3]) {
    QByteArray r(kReportPayload + 1, '\0');
    r[1] = 'C';
    r[2] = 'R';
    r[3] = 'T';
    r[6] = verb[0];
    r[7] = verb[1];
    r[8] = verb[2];
    return r;
}

/// Wake the display ("DIS").
inline QByteArray wakeScreen() {
    return command("DIS");
}

/// Display brightness, 0..100 percent ("LIG").
inline QByteArray brightness(int percent) {
    QByteArray r = command("LIG");
    r[11] = static_cast<char>(qBound(0, percent, 100));
    return r;
}

/// Clear one LCD key (1..6), or all of them with 0xFF ("CLE").
inline QByteArray clearKey(quint8 key) {
    QByteArray r = command("CLE");
    r[12] = static_cast<char>(key);
    return r;
}

/// Announce a JPEG of `jpegSize` bytes for LCD key 1..6 ("BAT"). The image follows in
/// imageChunks(); flush() makes it visible.
inline QByteArray imageHeader(quint8 key, quint32 jpegSize) {
    QByteArray r = command("BAT");
    r[9] = static_cast<char>((jpegSize >> 24) & 0xFF);
    r[10] = static_cast<char>((jpegSize >> 16) & 0xFF);
    r[11] = static_cast<char>((jpegSize >> 8) & 0xFF);
    r[12] = static_cast<char>(jpegSize & 0xFF);
    r[13] = static_cast<char>(key);
    return r;
}

/// The JPEG split into full-size reports, each prefixed with the report-ID byte and zero-padded.
inline QList<QByteArray> imageChunks(const QByteArray &jpeg) {
    QList<QByteArray> chunks;
    for (qsizetype start = 0; start < jpeg.size(); start += kReportPayload) {
        QByteArray r(kReportPayload + 1, '\0');
        const qsizetype n = qMin<qsizetype>(kReportPayload, jpeg.size() - start);
        std::memcpy(r.data() + 1, jpeg.constData() + start, static_cast<size_t>(n));
        chunks.append(r);
    }
    return chunks;
}

/// Show what was sent since the last flush ("STP").
inline QByteArray flush() {
    return command("STP");
}

} // namespace MiraboxN3Protocol

#endif // MIRABOXN3PROTOCOL_H
