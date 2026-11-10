#include "miraboxn3device.h"
#include "logging/logcategories.h"
#include "utils/threadshutdown.h"
#include <QThread>
#include <chrono>

namespace {
// WHY 2 s: shutdown() is queued behind whatever the worker is doing, and the slowest thing it does is
// drawing the six key images - a few dozen 1 KB HID writes.
constexpr std::chrono::milliseconds kHidStopTimeout{2000};
} // namespace

MiraboxN3Device::MiraboxN3Device(QObject *parent) : QObject(parent) {
    qRegisterMetaType<MiraboxN3Info>("MiraboxN3Info");

    m_hidThread = new QThread(this);
    m_hidThread->setObjectName("MiraboxN3Hid");
    m_worker = new MiraboxN3Worker();
    m_worker->moveToThread(m_hidThread);
    connect(m_hidThread, &QThread::started, m_worker, &MiraboxN3Worker::start);
    connect(m_hidThread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &MiraboxN3Worker::deviceInfoReady, this, [this](MiraboxN3Info info) {
        m_info = info;
        emit deviceInfoReady();
    });
    connect(m_worker, &MiraboxN3Worker::deviceArrived, this, [this]() {
        m_open = true;
        emit deviceConnected();
    });
    connect(m_worker, &MiraboxN3Worker::deviceRemoved, this, [this]() {
        m_open = false;
        emit deviceDisconnected();
    });
    connect(m_worker, &MiraboxN3Worker::buttonPressed, this, &MiraboxN3Device::buttonPressed);
    connect(m_worker, &MiraboxN3Worker::knobRotated, this, &MiraboxN3Device::knobRotated);
    connect(m_worker, &MiraboxN3Worker::pollError, this, &MiraboxN3Device::pollError);

    m_hidThread->start();
}

MiraboxN3Device::~MiraboxN3Device() {
    const QDeadlineTimer deadline(kHidStopTimeout);
    if (MiraboxN3Worker *worker = m_worker) {
        ThreadShutdown::callBlocking(worker, [worker]() { worker->shutdown(); }, deadline);
    }
    ThreadShutdown::retire(m_hidThread, deadline, lcMirabox());
    m_worker = nullptr;
}

void MiraboxN3Device::startPolling() {
    QMetaObject::invokeMethod(m_worker, &MiraboxN3Worker::openDevice, Qt::QueuedConnection);
}

void MiraboxN3Device::stopPolling() {
    QMetaObject::invokeMethod(m_worker, &MiraboxN3Worker::closeDevice, Qt::QueuedConnection);
}

void MiraboxN3Device::setKeyLabels(const QStringList &labels) {
    MiraboxN3Worker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, labels]() { worker->setKeyLabels(labels); }, Qt::QueuedConnection);
}
