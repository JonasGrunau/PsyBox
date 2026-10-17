#include "Arpeggiator.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace psy
{

ArpSettings ArpSettings::from (const ParamState& p) noexcept
{
    ArpSettings s;
    s.mode = p.geti (P::ArpMode);
    s.div = p.geti (P::ArpDiv);
    s.octaves = std::clamp (p.geti (P::ArpOctaves), 1, 4);
    s.pattern = ArpPattern::from (p);
    s.gate = p.get (P::ArpGate);
    s.swing = p.get (P::ArpSwing);
    s.hold = p.on (P::ArpHold);
    s.key = p.geti (P::ArpKey) - 1;
    s.scale = p.geti (P::ArpScale);
    if (p.geti (P::EngineMode) == (int) EngineMode::Kick)
    {
        // a tie would swallow the next kick: every step is a fresh hit
        s.gate = std::min (s.gate, 0.95f);
        for (int i = 0; i < kArpSteps; ++i)
            s.pattern.setFlag (i, ArpPattern::Slide, false);
    }
    // The Phoscyon engine accents from velocity 64 up, like the original.
    if (p.geti (P::EngineMode) == (int) EngineMode::Phoscyon)
        s.plainVelocity = 0.4f;
    return s;
}

void Arpeggiator::reset() noexcept
{
    numKeys = 0;
    sustain = false;
    synced = false;
    toStep = kIdle;
    toOff = -1.0;
    stepIndex = 0;
    seqPos = 0;
    current = -1;
    tied = false;
    lastRandom = -1;
    lastStep = -1;
    releasePending = false;
    rng.reseed (0xA59u);
}

void Arpeggiator::setSettings (const ArpSettings& s) noexcept
{
    const bool wasLatched = latched();
    set = s;
    updateStepSamples();
    if (wasLatched && ! latched())
        dropReleasedKeys();
}

void Arpeggiator::setSustain (bool down) noexcept
{
    const bool wasLatched = latched();
    sustain = down;
    if (wasLatched && ! latched())
        dropReleasedKeys();
}

void Arpeggiator::sync (double ppq, double newBpm, bool playing) noexcept
{
    bpm = newBpm > 0.0 ? newBpm : 120.0;
    updateStepSamples();
    const bool wasSynced = synced;
    synced = playing;
    if (! synced)
        return;

    const double beats = beatsPerStep();
    const double samplesPerBeat = 60.0 / bpm * fs;
    const auto fireBeat = [&] (int64_t s) { return ((double) s + swingOffset (s)) * beats; };

    // The host plays on from where the last block left it: the step the arp
    // is waiting for stays, its distance is just re-measured against the
    // host (a tempo change, rounding). Blocks end where they like, so that
    // step may be up to a sample late without having fired (the engine fires
    // within half a sample); it goes off at once instead of being skipped.
    if (wasSynced && toStep < kIdle * 0.5)
    {
        const double wait = (fireBeat (stepIndex) - ppq) * samplesPerBeat;
        if (wait >= -1.5 && wait <= stepSamples * 1.5 + 2.0)
        {
            toStep = wait;
            return;
        }
    }

    // Start, loop, seek, or a new division: the first step whose (swung)
    // time is not more than a sample in the past.
    const double slack = 1.0 / samplesPerBeat;
    int64_t k = (int64_t) std::floor (ppq / beats) - 1;
    while (fireBeat (k) < ppq - slack)
        ++k;
    stepIndex = k;
    toStep = (fireBeat (k) - ppq) * samplesPerBeat;
}

void Arpeggiator::keyOn (int note, float velocity) noexcept
{
    // Hold: the first key after letting go of everything starts a new set.
    if (set.hold && numKeys > 0 && numPhysicalKeys() == 0)
    {
        numKeys = 0;
        tied = false;   // the new chord starts fresh, it doesn't slide out of the old one
    }

    for (int i = 0; i < numKeys; ++i)
        if (keys[(size_t) i].note == note)
        {
            keys[(size_t) i].velocity = velocity;
            keys[(size_t) i].down = true;
            return;
        }

    const bool wasEmpty = numKeys == 0;
    if (numKeys == kMaxKeys)
        removeKey (0);
    keys[(size_t) numKeys++] = { note, velocity, true };
    releasePending = false;

    if (wasEmpty)
    {
        seqPos = 0;
        lastRandom = -1;
        if (! synced)
        {
            // free-running: the first key is step 0
            stepIndex = 0;
            toStep = 0.0;
        }
    }
}

void Arpeggiator::keyOff (int note) noexcept
{
    for (int i = 0; i < numKeys; ++i)
        if (keys[(size_t) i].note == note)
        {
            if (latched()) keys[(size_t) i].down = false;
            else removeKey (i);
            break;
        }
    if (numKeys == 0 && current >= 0)
        releasePending = true;
}

void Arpeggiator::allOff() noexcept
{
    numKeys = 0;
    if (current >= 0)
        releasePending = true;
}

void Arpeggiator::removeKey (int index) noexcept
{
    for (int i = index; i < numKeys - 1; ++i)
        keys[(size_t) i] = keys[(size_t) i + 1];
    --numKeys;
}

void Arpeggiator::dropReleasedKeys() noexcept
{
    for (int i = numKeys - 1; i >= 0; --i)
        if (! keys[(size_t) i].down)
            removeKey (i);
    if (numKeys == 0 && current >= 0)
        releasePending = true;
}

int Arpeggiator::numPhysicalKeys() const noexcept
{
    int n = 0;
    for (int i = 0; i < numKeys; ++i)
        n += keys[(size_t) i].down ? 1 : 0;
    return n;
}

int Arpeggiator::physicalKey (int index) const noexcept
{
    for (int i = 0; i < numKeys; ++i)
        if (keys[(size_t) i].down && index-- == 0)
            return keys[(size_t) i].note;
    return -1;
}

float Arpeggiator::physicalVelocity (int index) const noexcept
{
    for (int i = 0; i < numKeys; ++i)
        if (keys[(size_t) i].down && index-- == 0)
            return keys[(size_t) i].velocity;
    return 0.8f;
}

int Arpeggiator::samplesToNextEvent() const noexcept
{
    if (releasePending)
        return 1;
    double t = toStep;
    if (current >= 0 && toOff >= 0.0)
        t = std::min (t, toOff);
    if (t >= (double) INT_MAX)
        return INT_MAX;
    // events fire once the countdown is below half a sample
    return std::max (1, (int) std::floor (t - 0.5) + 1);
}

void Arpeggiator::advance (int numSamples) noexcept
{
    if (toStep < kIdle * 0.5)
        toStep -= numSamples;
    if (toOff >= 0.0)
        toOff = std::max (0.0, toOff - numSamples);
}

// The note for the next played step: the held keys (sorted, or in played
// order for Order) stacked over the octaves, walked by the mode.
int Arpeggiator::nextNote (float& velocity) noexcept
{
    std::array<Key, kMaxKeys> k {};
    std::copy (keys.begin(), keys.begin() + numKeys, k.begin());
    if ((ArpMode) set.mode != ArpMode::Order)
        std::stable_sort (k.begin(), k.begin() + numKeys, [] (const Key& a, const Key& b) { return a.note < b.note; });

    std::array<int, kMaxKeys * 4> notes {};
    std::array<float, kMaxKeys * 4> vels {};
    int len = 0;
    for (int o = 0; o < set.octaves; ++o)
        for (int i = 0; i < numKeys; ++i)
            if (const int n = k[(size_t) i].note + 12 * o; n <= 127)
            {
                notes[(size_t) len] = n;
                vels[(size_t) len++] = k[(size_t) i].velocity;
            }
    // (a key is at most 127, so octave 0 always adds every key: len >= 1)

    int idx = 0;
    switch ((ArpMode) set.mode)
    {
        case ArpMode::Up:
        case ArpMode::Order:
            idx = (int) (seqPos % (uint64_t) len);
            break;
        case ArpMode::Down:
            idx = len - 1 - (int) (seqPos % (uint64_t) len);
            break;
        case ArpMode::UpDown:
        {
            // 0 1 2 3 2 1 | 0 1 ... (the ends are not repeated)
            const int period = len > 1 ? 2 * len - 2 : 1;
            const int p = (int) (seqPos % (uint64_t) period);
            idx = p < len ? p : period - p;
            break;
        }
        case ArpMode::Random:
            idx = std::min ((int) (rng.uniform() * (float) len), len - 1);
            if (len > 1 && idx == lastRandom)
                idx = (idx + 1 + (int) (rng.next() % (uint32_t) (len - 1))) % len;
            lastRandom = idx;
            break;
    }
    ++seqPos;
    velocity = vels[(size_t) idx];
    return notes[(size_t) idx];
}

} // namespace psy
