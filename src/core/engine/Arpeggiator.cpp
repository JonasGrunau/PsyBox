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
    lastFired = INT64_MIN;
    seqPos = 0;
    current = -1;
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
    synced = playing;
    if (! synced)
        return;

    const double beats = beatsPerStep();
    const double samplesPerBeat = 60.0 / bpm * fs;
    const auto fireBeat = [&] (int64_t s) { return ((double) s + swingOffset (s)) * beats; };

    // First step whose (swung) time is not in the past.
    int64_t k = (int64_t) std::floor (ppq / beats) - 1;
    while (fireBeat (k) < ppq - 1.0e-9)
        ++k;

    if (lastFired != INT64_MIN && k <= lastFired)
    {
        if (k < lastFired - 1) lastFired = INT64_MIN;   // the host jumped back (loop, relocate)
        else k = lastFired + 1;                         // already fired at the block boundary
    }
    stepIndex = k;
    toStep = (fireBeat (k) - ppq) * samplesPerBeat;
}

void Arpeggiator::keyOn (int note, float velocity) noexcept
{
    // Hold: the first key after letting go of everything starts a new set.
    if (set.hold && numKeys > 0 && numPhysicalKeys() == 0)
        numKeys = 0;

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
            lastFired = INT64_MIN;
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
    if (len == 0)
    {
        velocity = k[0].velocity;
        return k[0].note;
    }

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
