#ifndef KNOBFUNCTION_H
#define KNOBFUNCTION_H

#include <QList>
#include <QString>

// What an external rotary knob (today the Mirabox N3's three knobs) adjusts: the K4's own front
// panel knobs. VFO A / VFO B / RIT-XIT tune through HardwareController's KPOD path; AF moves QK4's
// volume sliders (the K4's audio is streamed, so QK4's mix is the listening level); everything else
// steps through SideControlScrollController, the same handlers the side panel's scroll wheel uses.
//
// The settings key of each function is stored, not its number, so reordering this enum cannot
// silently re-map an operator's knobs.
enum class KnobFunction {
    None,
    VfoA,
    VfoB,
    RitXit,
    MainAf,
    SubAf,
    MainRfGain,
    SubRfGain,
    MainSquelch,
    SubSquelch,
    Bandwidth,
    Shift,
    HighCut,
    LowCut,
    Power,
    Delay,
    MicGain,
    Compression,
    KeyerSpeed,
    CwPitch,
};

namespace KnobFunctions {

struct Entry {
    KnobFunction function;
    const char *key;   // settings value
    const char *label; // Options page
};

inline const QList<Entry> &all() {
    static const QList<Entry> table = {
        {KnobFunction::None, "none", "Off"},
        {KnobFunction::VfoA, "vfoA", "VFO A"},
        {KnobFunction::VfoB, "vfoB", "VFO B (Sub)"},
        {KnobFunction::RitXit, "ritXit", "RIT / XIT offset"},
        {KnobFunction::MainAf, "mainAf", "AF Gain Main"},
        {KnobFunction::SubAf, "subAf", "AF Gain Sub"},
        {KnobFunction::MainRfGain, "mainRf", "RF Gain Main"},
        {KnobFunction::SubRfGain, "subRf", "RF Gain Sub"},
        {KnobFunction::MainSquelch, "mainSql", "Squelch Main"},
        {KnobFunction::SubSquelch, "subSql", "Squelch Sub"},
        {KnobFunction::Bandwidth, "bw", "Filter Bandwidth"},
        {KnobFunction::Shift, "shift", "Filter Shift"},
        {KnobFunction::HighCut, "hiCut", "Filter High Cut"},
        {KnobFunction::LowCut, "loCut", "Filter Low Cut"},
        {KnobFunction::Power, "power", "Power"},
        {KnobFunction::Delay, "delay", "QSK / VOX Delay"},
        {KnobFunction::MicGain, "mic", "Mic Gain"},
        {KnobFunction::Compression, "comp", "Compression"},
        {KnobFunction::KeyerSpeed, "wpm", "Keyer Speed"},
        {KnobFunction::CwPitch, "pitch", "CW Pitch"},
    };
    return table;
}

/// The function stored under `key`, or `fallback` for an empty or unknown key.
inline KnobFunction fromKey(const QString &key, KnobFunction fallback) {
    for (const Entry &e : all()) {
        if (key == QLatin1String(e.key))
            return e.function;
    }
    return fallback;
}

/// What Mirabox N3 knob 1..3 does while its setting is unset: tune VFO A, VFO B, RIT/XIT.
inline KnobFunction miraboxN3Default(int knob) {
    switch (knob) {
    case 1:
        return KnobFunction::VfoA;
    case 2:
        return KnobFunction::VfoB;
    case 3:
        return KnobFunction::RitXit;
    default:
        return KnobFunction::None;
    }
}

inline QString key(KnobFunction function) {
    for (const Entry &e : all()) {
        if (e.function == function)
            return QLatin1String(e.key);
    }
    return QStringLiteral("none");
}

} // namespace KnobFunctions

#endif // KNOBFUNCTION_H
