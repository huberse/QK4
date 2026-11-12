#include "miraboxn3worker.h"
#include "logging/logcategories.h"
#include "miraboxn3protocol.h"
#include <QBuffer>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QLoggingCategory>
#include <QPainter>
#include <QThread>
#include <QTimer>
#include <QTransform>
#include <hidapi/hidapi.h>

namespace P = MiraboxN3Protocol;

MiraboxN3Worker::MiraboxN3Worker(QObject *parent) : QObject(parent) {}

MiraboxN3Worker::~MiraboxN3Worker() {
    releaseHandle();
    if (m_hidInitialized) {
        hid_exit();
        m_hidInitialized = false;
    }
}

// =============================================================================
// Lifecycle
// =============================================================================

void MiraboxN3Worker::start() {
    // Reference-counted inside hidapi, so this coexists with the KPOD worker's own hid_init.
    if (hid_init() != 0) {
        qCWarning(lcMirabox) << "hid_init failed";
        return;
    }
    m_hidInitialized = true;

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &MiraboxN3Worker::onPollTimer);

    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(kPresenceIntervalMs);
    connect(m_presenceTimer, &QTimer::timeout, this, &MiraboxN3Worker::onPresenceTimer);
    m_presenceTimer->start();

    m_info = detect();
    m_present = m_info.detected;
    emit deviceInfoReady(m_info);
}

void MiraboxN3Worker::shutdown() {
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_presenceTimer)
        m_presenceTimer->stop();
    if (m_hid) {
        // Leave the keys dark rather than showing stale labels for a program that has quit.
        writeReport(P::clearKey(0xFF));
        writeReport(P::flush());
    }
    releaseHandle();
    if (m_hidInitialized) {
        hid_exit();
        m_hidInitialized = false;
    }
}

// =============================================================================
// Detection
// =============================================================================

MiraboxN3Info MiraboxN3Worker::detect() {
    MiraboxN3Info info;
    hid_device_info *devs = hid_enumerate(P::kVendorId, P::kProductId);
    hid_device_info *selected = nullptr;
    for (hid_device_info *d = devs; d != nullptr; d = d->next) {
        // Prefer the vendor-defined interface; fall back to the first one if the platform reports
        // no usage page (hidapi-libusb on older kernels).
        if (d->usage_page == P::kUsagePage) {
            selected = d;
            break;
        }
        if (!selected)
            selected = d;
    }
    if (selected) {
        info.detected = true;
        if (selected->product_string)
            info.productName = QString::fromWCharArray(selected->product_string);
        if (selected->manufacturer_string)
            info.manufacturer = QString::fromWCharArray(selected->manufacturer_string);
        if (selected->serial_number)
            info.serial = QString::fromWCharArray(selected->serial_number);
        if (selected->path)
            info.devicePath = QString::fromUtf8(selected->path);
        qCInfo(lcMirabox) << "Mirabox N3 found:" << info.productName << info.manufacturer << "usage page" << Qt::hex
                          << selected->usage_page;
    }
    hid_free_enumeration(devs);
    return info;
}

void MiraboxN3Worker::onPresenceTimer() {
    hid_device_info *devs = hid_enumerate(P::kVendorId, P::kProductId);
    const bool now = (devs != nullptr);
    hid_free_enumeration(devs);

    if (!m_present && now) {
        m_info = detect();
        m_present = m_info.detected;
        emit deviceInfoReady(m_info);
    } else if (m_present && !now) {
        handleLostDevice("removed from the bus");
    }
}

void MiraboxN3Worker::handleLostDevice(const char *reason) {
    // Idempotent: a failed read and the presence timer both report one unplug.
    if (!m_present && !m_hid)
        return;
    qCWarning(lcMirabox) << "Mirabox N3 lost:" << reason;
    m_present = false;
    const bool wasOpen = (m_hid != nullptr);
    if (m_pollTimer)
        m_pollTimer->stop();
    releaseHandle();
    m_info = MiraboxN3Info{};
    emit deviceInfoReady(m_info);
    if (wasOpen)
        emit deviceRemoved();
}

// =============================================================================
// Open / close
// =============================================================================

void MiraboxN3Worker::openDevice() {
    if (m_hid)
        return;
    if (m_info.devicePath.isEmpty()) {
        emit openFailed();
        return;
    }
    m_hid = hid_open_path(m_info.devicePath.toUtf8().constData());
    if (!m_hid) {
        qCWarning(lcMirabox) << "hid_open_path failed for" << m_info.devicePath;
        emit openFailed();
        return;
    }
    hid_set_nonblocking(m_hid, 1);
    initDisplay();
    m_pollTimer->start();
    emit deviceArrived();
}

void MiraboxN3Worker::closeDevice() {
    const bool wasOpen = (m_hid != nullptr);
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_hid) {
        writeReport(P::clearKey(0xFF));
        writeReport(P::flush());
    }
    releaseHandle();
    if (wasOpen)
        emit deviceRemoved();
}

void MiraboxN3Worker::releaseHandle() {
    if (m_hid) {
        hid_close(m_hid);
        m_hid = nullptr;
    }
}

// =============================================================================
// Input
// =============================================================================

void MiraboxN3Worker::onPollTimer() {
    if (!m_hid)
        return;
    unsigned char buf[512];
    // Drain everything queued since the last tick; a fast knob spin produces several reports.
    for (int i = 0; i < 32; ++i) {
        const int n = hid_read(m_hid, buf, sizeof(buf));
        if (n < 0) {
            handleLostDevice("hid_read failed");
            emit pollError(QStringLiteral("Failed to read from Mirabox N3"));
            return;
        }
        if (n == 0)
            return;

        const P::InputEvent ev = P::decodeInput(buf, n);
        switch (ev.type) {
        case P::InputEvent::Type::Button:
            qCDebug(lcMirabox) << "button" << ev.index << (ev.pressed ? "pressed" : "released");
            // Act on the press. Some N3 firmware sends no release, so a release-based tap or a
            // hold timer would misfire there.
            if (ev.pressed)
                emit buttonPressed(ev.index);
            break;
        case P::InputEvent::Type::Knob:
            qCDebug(lcMirabox) << "knob" << ev.index << ev.ticks;
            emit knobRotated(ev.index, ev.ticks);
            break;
        case P::InputEvent::Type::Unknown:
            // Info, not debug: an unknown code is exactly what a tester of a new firmware should
            // be able to report without turning on debug logging.
            qCInfo(lcMirabox) << "unknown input code" << Qt::hex << ev.rawCode << "state" << buf[10];
            break;
        case P::InputEvent::Type::None:
            break;
        }
    }
}

// =============================================================================
// Display
// =============================================================================

bool MiraboxN3Worker::writeReport(const QByteArray &report) {
    if (!m_hid)
        return false;
    const int res = hid_write(m_hid, reinterpret_cast<const unsigned char *>(report.constData()),
                              static_cast<size_t>(report.size()));
    if (res < 0) {
        qCWarning(lcMirabox) << "hid_write failed:" << QString::fromWCharArray(hid_error(m_hid));
        return false;
    }
    return true;
}

void MiraboxN3Worker::initDisplay() {
    writeReport(P::wakeScreen());
    writeReport(P::brightness(m_brightness));
    writeReport(P::clearKey(0xFF));
    drawKeys();
}

void MiraboxN3Worker::setKeyLabels(const QStringList &labels) {
    m_labels = labels;
    if (m_hid)
        drawKeys();
}

void MiraboxN3Worker::setBrightness(int percent) {
    m_brightness = qBound(0, percent, 100);
    if (m_hid)
        writeReport(P::brightness(m_brightness));
}

void MiraboxN3Worker::drawKeys() {
    // A flush after each key, as wfview does: the N3 shows an image only once its STP arrives.
    for (int key = 1; key <= P::kLcdKeys; ++key) {
        drawKey(key, key - 1 < m_labels.size() ? m_labels.at(key - 1) : QString());
        writeReport(P::flush());
    }
}

void MiraboxN3Worker::drawKey(int key, const QString &text) {
    // QPainter on a QImage is safe off the GUI thread.
    QImage img(P::kKeyImageSize, P::kKeyImageSize, QImage::Format_RGB888);
    img.fill(QColor(0x20, 0x20, 0x20));
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setPen(Qt::white);
        QFont font;
        font.setPixelSize(text.size() > 6 ? 14 : 18);
        font.setBold(true);
        p.setFont(font);
        const QString shown = text.isEmpty() ? QString::number(key) : text;
        p.drawText(img.rect().adjusted(3, 3, -3, -3), Qt::AlignCenter | Qt::TextWordWrap, shown);
    }

    // The panel is mounted rotated; wfview turns N3 images by 90 degrees.
    const QImage rotated = img.transformed(QTransform().rotate(90));
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!rotated.save(&buffer, "JPG", 90)) {
        // No qjpeg image-format plugin deployed; the keys still work, only unlabelled.
        qCWarning(lcMirabox) << "JPEG encoding unavailable; N3 key labels not drawn";
        return;
    }

    if (!writeReport(P::imageHeader(static_cast<quint8>(key), static_cast<quint32>(jpeg.size()))))
        return;
    for (const QByteArray &chunk : P::imageChunks(jpeg)) {
        if (!writeReport(chunk))
            return;
    }
}
