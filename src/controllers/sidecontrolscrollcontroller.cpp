#include "sidecontrolscrollcontroller.h"

#include "connectioncontroller.h"
#include "models/radiostate.h"
#include "network/catframes.h"
#include "ui/widgets/sidecontrolpanel.h"
#include "utils/radioutils.h"

#include <QChar>
#include <QString>

namespace {
// The K4 front panel's BW knob step.
constexpr int kBandwidthStepHz = 50;

// IS+nnnn / IS$+nnnn, nnnn the filter centre in 10 Hz units.
// WHY the '+': the Programmer's Reference D12 gives the SET as IS$nnnn with no sign, and the
// radio's replies have none (RadioState parses them unsigned). QK4 has always sent the signed
// form, recorded from bench work as the one the radio accepts. Bench both before dropping it.
// Not a CatFrames builder yet because nothing else sends IS.
QString ifShiftSet(bool subRx, int centreDah) {
    return QString("%1+%2;").arg(subRx ? "IS$" : "IS").arg(centreDah, 4, 10, QChar('0'));
}
} // namespace

SideControlScrollController::SideControlScrollController(RadioState *radioState, ConnectionController *connection,
                                                         SideControlPanel *panel, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_panel(panel) {
    // Every side panel knob routes through adjust(), which the Mirabox N3's knobs call too, so a
    // knob and a scroll wheel can never step the same control differently.
    connect(m_panel, &SideControlPanel::wpmChanged, this,
            [this](int delta) { adjust(KnobFunction::KeyerSpeed, delta); });
    connect(m_panel, &SideControlPanel::pitchChanged, this,
            [this](int delta) { adjust(KnobFunction::CwPitch, delta); });
    connect(m_panel, &SideControlPanel::micGainChanged, this,
            [this](int delta) { adjust(KnobFunction::MicGain, delta); });
    connect(m_panel, &SideControlPanel::compressionChanged, this,
            [this](int delta) { adjust(KnobFunction::Compression, delta); });
    connect(m_panel, &SideControlPanel::powerChanged, this, [this](int delta) { adjust(KnobFunction::Power, delta); });
    connect(m_panel, &SideControlPanel::delayChanged, this, [this](int delta) { adjust(KnobFunction::Delay, delta); });
    connect(m_panel, &SideControlPanel::bandwidthChanged, this,
            [this](int delta) { adjust(KnobFunction::Bandwidth, delta); });
    connect(m_panel, &SideControlPanel::highCutChanged, this,
            [this](int delta) { adjust(KnobFunction::HighCut, delta); });
    connect(m_panel, &SideControlPanel::shiftChanged, this, [this](int delta) { adjust(KnobFunction::Shift, delta); });
    connect(m_panel, &SideControlPanel::lowCutChanged, this,
            [this](int delta) { adjust(KnobFunction::LowCut, delta); });
    connect(m_panel, &SideControlPanel::mainRfGainChanged, this,
            [this](int delta) { adjust(KnobFunction::MainRfGain, delta); });
    connect(m_panel, &SideControlPanel::mainSquelchChanged, this,
            [this](int delta) { adjust(KnobFunction::MainSquelch, delta); });
    connect(m_panel, &SideControlPanel::subRfGainChanged, this,
            [this](int delta) { adjust(KnobFunction::SubRfGain, delta); });
    connect(m_panel, &SideControlPanel::subSquelchChanged, this,
            [this](int delta) { adjust(KnobFunction::SubSquelch, delta); });
}

bool SideControlScrollController::adjust(KnobFunction function, int delta) {
    // After sending CAT, update RadioState optimistically - the K4 does not echo most of these
    // SET commands. Every knob steps from the value the radio reported, within the K4's own limits
    // (RadioUtils), and sends nothing until the radio has reported it.
    //
    // RF Gain uses RG-nn; where nn is 00-60 (-0 to -60 dB attenuation): scroll up = less
    // attenuation = a smaller value. BW/HI and SHFT/LO adjust VFO B while B SET is on.
    switch (function) {
    case KnobFunction::KeyerSpeed: {
        const auto wpm = RadioUtils::nextKeyerSpeed(m_radioState->keyerSpeed(), delta);
        if (!wpm)
            return true;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::keyerSpeed(*wpm)));
        m_radioState->setKeyerSpeed(*wpm);
        return true;
    }
    case KnobFunction::CwPitch: {
        const auto pitchHz = RadioUtils::nextCwPitch(m_radioState->cwPitch(), delta);
        if (!pitchHz)
            return true;
        m_connection->sendCAT(QString("CW%1;").arg(*pitchHz / RadioUtils::CW_PITCH_STEP_HZ, 2, 10, QChar('0')));
        m_radioState->setCwPitch(*pitchHz);
        return true;
    }
    case KnobFunction::MicGain: {
        const auto gain = RadioUtils::stepWithin(m_radioState->micGain(), delta, 0, RadioUtils::MIC_GAIN_MAX);
        if (!gain)
            return true;
        m_connection->sendCAT(QString("MG%1;").arg(*gain, 3, 10, QChar('0')));
        m_radioState->setMicGain(*gain);
        return true;
    }
    case KnobFunction::Compression: {
        const auto comp = RadioUtils::stepWithin(m_radioState->compression(), delta, 0, RadioUtils::COMPRESSION_MAX);
        if (!comp)
            return true;
        m_connection->sendCAT(QString("CP%1;").arg(*comp, 3, 10, QChar('0')));
        m_radioState->setCompression(*comp);
        return true;
    }
    case KnobFunction::Power: {
        const auto next = RadioUtils::nextRfPower(m_radioState->rfPower(), m_radioState->powerRange(), delta);
        if (!next)
            return true;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::setRfPower(next->value, next->range)));
        m_radioState->setRfPower(next->value, next->range);
        return true;
    }
    case KnobFunction::Delay: {
        const auto delay =
            RadioUtils::stepWithin(m_radioState->delayForCurrentMode(), delta, 0, RadioUtils::QSK_DELAY_MAX);
        if (!delay)
            return true;
        const int newDelay = *delay;
        m_radioState->setDelayForCurrentMode(newDelay);

        // SDxyzzz: x=0 keeps the delay (not full QSK), y is the mode class letter, zzz in 10 ms.
        const char modeLetter = RadioUtils::modeClassLetter(m_radioState->modeClass());
        m_connection->sendCAT(QString("SD0%1%2;").arg(QLatin1Char(modeLetter)).arg(newDelay, 3, 10, QChar('0')));
        return true;
    }
    case KnobFunction::Bandwidth: {
        const bool bSet = m_radioState->bSetEnabled();
        const RadioUtils::FilterEdgeLimits limits = targetFilterLimits();
        const auto bwHz =
            RadioUtils::stepWithin(bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth(),
                                   delta * kBandwidthStepHz, limits.bwMinDah * 10, limits.bwMaxDah * 10);
        if (!bwHz)
            return true;
        const QByteArray bw = CatFrames::setFilterBandwidth(*bwHz);
        m_connection->sendCAT(QString::fromLatin1(bSet ? CatFrames::subRx(bw) : bw));
        if (bSet) {
            m_radioState->setFilterBandwidthB(*bwHz);
        } else {
            m_radioState->setFilterBandwidth(*bwHz);
        }
        return true;
    }
    case KnobFunction::HighCut: {
        applyFilterEdgeChange(/*adjustHi=*/true, delta);
        return true;
    }
    case KnobFunction::Shift: {
        const bool bSet = m_radioState->bSetEnabled();
        const RadioUtils::FilterEdgeLimits limits = targetFilterLimits();
        if (limits.isLocked)
            return true;
        const auto shift = RadioUtils::stepWithin(bSet ? m_radioState->ifShiftB() : m_radioState->ifShift(), delta,
                                                  limits.isMinDah, limits.isMaxDah);
        if (!shift)
            return true;
        m_connection->sendCAT(ifShiftSet(bSet, *shift));
        if (bSet) {
            m_radioState->setIfShiftB(*shift);
        } else {
            m_radioState->setIfShift(*shift);
        }
        return true;
    }
    case KnobFunction::LowCut: {
        applyFilterEdgeChange(/*adjustHi=*/false, delta);
        return true;
    }
    case KnobFunction::MainRfGain: {
        const auto gain = RadioUtils::stepWithin(m_radioState->rfGain(), -delta, 0, RadioUtils::RF_GAIN_MAX);
        if (!gain)
            return true;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::rfGain(*gain)));
        m_radioState->setRfGain(*gain);
        return true;
    }
    case KnobFunction::MainSquelch: {
        const auto sql = RadioUtils::nextSquelch(m_radioState->squelchLevel(), delta);
        if (!sql)
            return true;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::squelch(*sql)));
        m_radioState->setSquelchLevel(*sql);
        return true;
    }
    case KnobFunction::SubRfGain: {
        const auto gain = RadioUtils::stepWithin(m_radioState->rfGainB(), -delta, 0, RadioUtils::RF_GAIN_MAX);
        if (!gain)
            return true;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::subRx(CatFrames::rfGain(*gain))));
        m_radioState->setRfGainB(*gain);
        return true;
    }
    case KnobFunction::SubSquelch: {
        const auto sql = RadioUtils::nextSquelch(m_radioState->squelchLevelB(), delta);
        if (!sql)
            return true;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::subRx(CatFrames::squelch(*sql))));
        m_radioState->setSquelchLevelB(*sql);
        return true;
    }
    default:
        return false;
    }
}

RadioUtils::FilterEdgeLimits SideControlScrollController::targetFilterLimits() const {
    return RadioUtils::filterLimits(m_radioState->filterTargetMode(), m_radioState->filterTargetDataSubMode());
}

void SideControlScrollController::applyFilterEdgeChange(bool adjustHi, int delta) {
    // HI and LO adjust filter edges while keeping the OPPOSITE edge fixed.
    // Both BW and IS must change. Work in decahertz (dah) to avoid rounding
    // drift. Step is 2 dah (20 Hz) per scroll tick — keeps IS on-grid.
    const bool bSet = m_radioState->bSetEnabled();
    const int bwHz = bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth();
    const int isDah = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();
    if (bwHz < 0 || isDah < 0)
        return; // not reported yet
    const RadioUtils::FilterEdgeLimits limits = targetFilterLimits();
    const bool isLocked = limits.isLocked;
    const auto next = RadioUtils::filterAfterEdgeStep({bwHz / 10, isDah}, adjustHi, delta, limits);
    if (!next)
        return;
    const int newBwDah = next->bwDah;

    const QByteArray bw = CatFrames::setFilterBandwidth(newBwDah * 10);
    m_connection->sendCAT(QString::fromLatin1(bSet ? CatFrames::subRx(bw) : bw));

    if (isLocked) {
        // IS stays fixed — only BW changes
        if (bSet)
            m_radioState->setFilterBandwidthB(newBwDah * 10);
        else
            m_radioState->setFilterBandwidth(newBwDah * 10);
    } else {
        const int newIsDah = next->isDah;
        m_connection->sendCAT(ifShiftSet(bSet, newIsDah));

        if (bSet) {
            m_radioState->setFilterBandwidthB(newBwDah * 10);
            m_radioState->setIfShiftB(newIsDah);
        } else {
            m_radioState->setFilterBandwidth(newBwDah * 10);
            m_radioState->setIfShift(newIsDah);
        }
    }
}
