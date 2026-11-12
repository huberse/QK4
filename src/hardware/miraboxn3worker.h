#ifndef MIRABOXN3WORKER_H
#define MIRABOXN3WORKER_H

#include <QObject>
#include <QString>
#include <QStringList>

typedef struct hid_device_ hid_device;

class QTimer;

struct MiraboxN3Info {
    bool detected = false;
    QString productName;
    QString manufacturer;
    QString devicePath;
    QString serial;
};

// Owns the hidapi handle of a Mirabox N3 and runs every hid_* call on its own QThread, as
// KpodHidWorker does for the KPOD. Public slots are dispatched from MiraboxN3Device by queued
// connection; the main thread never touches hidapi.
//
// Presence is a 2 s hid_enumerate poll on every platform (no udev monitor): the N3 is a prototype
// integration and a 2 s arrival delay is harmless for a button box.
//
// The worker REPORTS arrival and loss; whether to open is decided by UsbDeviceLifecycle in
// HardwareController (USB-002), never here.
class MiraboxN3Worker : public QObject {
    Q_OBJECT

public:
    explicit MiraboxN3Worker(QObject *parent = nullptr);
    ~MiraboxN3Worker() override;

public slots:
    void start();    // wired to QThread::started — hid_init, timers, initial detection
    void shutdown(); // run on this thread by the façade dtor

    void openDevice();
    void closeDevice();

    /// Text for LCD keys 1..6 (index 0 = key 1). Drawn now if the device is open, and again on
    /// every open.
    void setKeyLabels(const QStringList &labels);
    void setBrightness(int percent);

signals:
    void deviceInfoReady(MiraboxN3Info info);
    void deviceArrived();
    void deviceRemoved();
    void openFailed();
    void buttonPressed(int button);
    void knobRotated(int knob, int ticks);
    void pollError(QString message);

private slots:
    void onPollTimer();
    void onPresenceTimer();

private:
    MiraboxN3Info detect();
    bool writeReport(const QByteArray &report);
    void initDisplay();
    void drawKeys();
    void drawKey(int key, const QString &text);
    void handleLostDevice(const char *reason);
    void releaseHandle();

    hid_device *m_hid = nullptr;
    MiraboxN3Info m_info;
    bool m_present = false;
    bool m_hidInitialized = false;

    QStringList m_labels;
    int m_brightness = 80;

    QTimer *m_pollTimer = nullptr;
    QTimer *m_presenceTimer = nullptr;
    static constexpr int kPollIntervalMs = 10;
    static constexpr int kPresenceIntervalMs = 2000;
};

#endif // MIRABOXN3WORKER_H
