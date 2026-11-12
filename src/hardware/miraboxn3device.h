#ifndef MIRABOXN3DEVICE_H
#define MIRABOXN3DEVICE_H

#include "miraboxn3worker.h" // MiraboxN3Info
#include <QObject>
#include <QStringList>

class QThread;

// Thin Qt façade over MiraboxN3Worker, the same split as KpodDevice / KpodHidWorker: the worker owns
// the hidapi handle on m_hidThread, this object caches its reports for the main thread and forwards
// requests to it by queued invocation.
class MiraboxN3Device : public QObject {
    Q_OBJECT

public:
    explicit MiraboxN3Device(QObject *parent = nullptr);
    ~MiraboxN3Device() override;

    bool isDetected() const { return m_info.detected; }
    bool isOpen() const { return m_open; }
    MiraboxN3Info deviceInfo() const { return m_info; }

    void startPolling();
    void stopPolling();
    void setKeyLabels(const QStringList &labels);

signals:
    void deviceInfoReady();
    void deviceConnected();
    void deviceDisconnected();
    void openFailed(); // e.g. the vendor's Stream Dock software holds the device
    void buttonPressed(int button);
    void knobRotated(int knob, int ticks);
    void pollError(const QString &error);

private:
    MiraboxN3Info m_info;
    bool m_open = false;
    MiraboxN3Worker *m_worker = nullptr;
    QThread *m_hidThread = nullptr;
};

#endif // MIRABOXN3DEVICE_H
