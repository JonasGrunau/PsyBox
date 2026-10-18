#include "Engine.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace psy
{

namespace
{
    constexpr int kChunk = 16;  // control-rate granularity for parameter smoothing

    int factorForQuality (int q) noexcept { return q == 0 ? 1 : (q == 1 ? 2 : 4); }
    float harmonicLimitFor (int factor) noexcept { return factor == 1 ? 0.45f : (factor == 2 ? 0.62f : 0.8f); }

    constexpr float kControllerMs = 5.f;   // mod wheel, aftertouch and bend pass through a pole this long

    // True when any sample is NaN or infinity. Bit test, so it holds under fast-math too.
    bool anyNonFinite (const float* x, int n) noexcept
    {
        uint32_t bad = 0;
        for (int i = 0; i < n; ++i)
            bad |= (uint32_t) (((std::bit_cast<uint32_t> (x[i]) >> 23) & 0xFFu) == 0xFFu);
        return bad != 0;
    }
} // namespace

Engine::Engine()
{
    ctx.p = &cur;
}

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    maxBlock = std::max (maxBlockSize, kChunk);

    osL.assign ((size_t) maxBlock * 4, 0.f);
    osR.assign ((size_t) maxBlock * 4, 0.f);
    decimL.prepare (maxBlock);
    decimR.prepare (maxBlock);
    for (auto& b : globalLfoBuf)
        b.assign ((size_t) maxBlock, 0.f);
    fxSrc.assign ((size_t) maxBlock, {});

    for (auto& v : voices)
        v.prepare ((float) fs, (float) fs * 4.f);
    for (auto& k : kicks)
        k.prepare ((float) fs, (float) fs * 2.f);
    phos.prepare ((float) fs, (float) fs * 2.f);
    phosModEnv.setSampleRate ((float) fs);
    phosFx.prepare ((float) fs, (float) fs * 2.f);

    for (int i = 0; i < kNumLfos; ++i)
    {
        globalLfos[(size_t) i].setSampleRate ((float) fs);
        globalLfos[(size_t) i].seed (1000u + (uint64_t) i);
    }

    arp.prepare (fs);

    const float f = (float) fs;
    distLate.prepare (f);
    crush.prepare (f);
    fshift.prepare (f);
    phaser.prepare (f);
    chorus.prepare (f);
    comp.prepare (f);
    delay.prepare (f);
    reverb.prepare (f);
    width.prepare (f);
    for (auto& d : outDc) d.prepare (f);

    osFactor = wantedFactor();
    applyQuality();
    reset();
    cur = target;  // notes in the very first block must see the real patch
    firstBlock = false;
}

void Engine::setParams (const ParamState& p) noexcept
{
    target = p;
    const auto& defs = paramDefs();
    for (size_t i = 0; i < (size_t) kNumParams; ++i)
    {
        float& v = target.v[i];
        v = std::isfinite (v) ? std::clamp (v, defs[i].min, defs[i].max) : defs[i].def;
    }
}

int Engine::wantedFactor() const noexcept
{
    return factorForQuality (target.geti (offline ? P::RenderQuality : P::Quality));
}

void Engine::applyQuality()
{
    const float osRate = (float) fs * (float) osFactor;
    for (auto& v : voices)
        v.setRates ((float) fs, osRate);
    for (auto& k : kicks)
        k.setRates ((float) fs, osRate);
    phos.setRates ((float) fs, osRate);
    phosFx.setOsRate (osRate);
    ctx.baseRate = (float) fs;
    ctx.osRate = osRate;
    ctx.osFactor = osFactor;
    ctx.harmonicLimit = harmonicLimitFor (osFactor);
    dist.prepare (osRate);  // runs before decimation, so it is oversampled for free
    distLate.setQuality (osFactor);   // anywhere else it oversamples itself, more than the voices
    decimL.reset();
    decimR.reset();
}

void Engine::resetEffects() noexcept
{
    phosFx.reset();
    dist.reset();
    distLate.reset();
    crush.reset();
    fshift.reset();
    phaser.reset();
    chorus.reset();
    comp.reset();
    delay.reset();
    reverb.reset();
    width.reset();
    for (auto& d : outDc) d.reset();
    decimL.reset();
    decimR.reset();
    outGainSet = false;
    fxCount = fxCountOs = 0;
    phosModCount = 0;
}

void Engine::reset()
{
    leadVoice = -1;
    for (auto& v : voices) v.kill();
    for (auto& k : kicks) k.kill();
    phos.kill();
    phosModEnv.kill();
    engineMode = -1;
    numHeld = 0;
    sustained.fill (false);
    sustainPedal = false;
    arp.reset();
    arpActive = false;
    // the next patch's first note starts where its own pitch is, not on a glide from the old one
    lastNote = -1;
    lastPitch = -1.f;
    // free LFOs start from their Phase, like a fresh patch
    for (int i = 0; i < kNumLfos; ++i)
        globalLfos[(size_t) i].reset (target.get (lfoParam (i, 5)));
    resetEffects();
    qualityPending = false;
    lastPlaying = false;
    lfoLockIn = 0;
    diag = {};
    fxBad = false;
    firstBlock = true;
}

void Engine::getModView (ModView& out) const noexcept
{
    if (phosMode())
    {
        // the 303 voice: its amp and filter envelopes, the free LFOs
        out.numVoices = phos.isActive() ? 1 : 0;
        if (out.numVoices > 0)
        {
            using Stage = dsp::Adsr::Stage;
            auto& o = out.voices[0];
            o.env[0] = { (uint8_t) (phos.isGateOn() ? Stage::Decay : Stage::Release), 0.f };
            o.env[1] = { (uint8_t) Stage::Decay, 1.f - phos.filterEnv() };
            o.env[2] = { (uint8_t) phosModEnv.getStage(), phosModEnv.stageProgress() };
            o.env[3] = {};
            for (int i = 0; i < kNumLfos; ++i)
                o.lfo[(size_t) i] = globalLfos[(size_t) i].phase2();
            o.filter = {};
            o.filter[0].cutoff = phos.cutoffHz();
        }
        return;
    }
    std::array<int, kMaxVoices> order {};
    int n = 0;
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices[(size_t) i].isActive())
            order[(size_t) n++] = i;
    std::sort (order.begin(), order.begin() + n,
               [this] (int a, int b) { return voices[(size_t) a].age > voices[(size_t) b].age; });

    out.numVoices = n;
    for (int k = 0; k < n; ++k)
    {
        const auto& v = voices[(size_t) order[(size_t) k]];
        auto& o = out.voices[(size_t) k];
        for (int e = 0; e < 3; ++e)
            o.env[(size_t) e] = { (uint8_t) v.envelope (e).getStage(), v.envelope (e).stageProgress() };
        const auto& pe = v.pitchEnvelope();
        o.env[3] = { (uint8_t) pe.getStage(), pe.stageProgress() };
        // a free-running LFO (Retrig off) is the same for every voice
        for (int i = 0; i < kNumLfos; ++i)
            o.lfo[(size_t) i] = cur.on (lfoParam (i, 4)) ? v.lfo (i).phase2() : globalLfos[(size_t) i].phase2();
        o.sweep = v.sweepPhase();
        o.filter = { v.filterNow (0), v.filterNow (1) };
    }
}

int Engine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.isActive() ? 1 : 0;
    for (const auto& k : kicks)
        n += k.isActive() ? 1 : 0;
    return n + (phos.isActive() ? 1 : 0);
}

// Discrete parameters (and so the arp's division, mode and on switch) take
// their new value when a block starts, before the arp is synced to the grid.
void Engine::snapDiscrete() noexcept
{
    const auto& defs = paramDefs();
    for (size_t i = 0; i < (size_t) kNumParams; ++i)
        if (defs[i].isDiscrete())
            cur.v[i] = target.v[i];
}

void Engine::smoothParams (int numSamples) noexcept
{
    const auto& defs = paramDefs();
    const float a = 1.f - std::exp (-(float) numSamples / (0.012f * (float) fs));
    for (size_t i = 0; i < (size_t) kNumParams; ++i)
    {
        const float t = target.v[i];
        if (cur.v[i] == t)
            continue;   // settled: nothing to move (most of the table, most of the time)
        if (defs[i].isDiscrete())
        {
            cur.v[i] = t;
        }
        else if (defs[i].mapping == Mapping::Exp)
        {
            // smooth frequencies in the log domain so sweeps sound even
            const float lc = std::log (cur.v[i]), lt = std::log (t);
            const float next = std::exp (lc + (lt - lc) * a);
            // the log-domain pole closes in on the target without always landing on it
            cur.v[i] = std::abs (next - t) <= 1e-6f * t ? t : next;
        }
        else
        {
            cur.v[i] += (t - cur.v[i]) * a;
        }
    }
}

// Mod wheel, aftertouch and bend arrive in 7-bit steps: a pole rounds them.
void Engine::smoothControllers (int n) noexcept
{
    const float a = 1.f - std::exp (-(float) n / (kControllerMs * 0.001f * (float) fs));
    ctx.modWheel += (rawWheel - ctx.modWheel) * a;
    ctx.aftertouch += (rawAftertouch - ctx.aftertouch) * a;
    ctx.pitchBend += (rawBend - ctx.pitchBend) * a;
}

void Engine::prepareContext (double bpm) noexcept
{
    ctx.bpm = (float) bpm;
    for (int i = 0; i < kNumLfos; ++i)
    {
        const bool sync = cur.on (lfoParam (i, 2));
        ctx.lfoHz[(size_t) i] = sync ? (float) (bpm / 60.0) / syncDivBeats (cur.geti (lfoParam (i, 3)))
                                     : cur.get (lfoParam (i, 1));
    }
    ctx.numSlots = 0;
    numFxSlots = 0;
    ctx.sweepUsed = cur.get (P::EnergyFade) != 0.f;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const int src = cur.geti (modSrcParam (s));
        const int dst = cur.geti (modDstParam (s));
        const float amt = cur.get (modAmtParam (s));
        if (src <= 0 || dst <= 0 || amt == 0.f || src >= (int) ModSrc::Count || dst >= (int) ModDst::Count)
            continue;
        ctx.sweepUsed = ctx.sweepUsed || src == (int) ModSrc::Sweep;
        if (isFxTarget ((ModDst) dst))
            fxSlots[(size_t) numFxSlots++] = { src, dst, amt };
        else
            ctx.slots[(size_t) ctx.numSlots++] = { src, dst, amt };
    }

    // A free-running LFO is one oscillator for every voice, so only sources
    // that are the same for every voice can move its rate: the macros, wheel,
    // aftertouch and bend (a voice's own LFO takes any source, in Voice).
    freeLfoHz = ctx.lfoHz;
    for (int i = 0; i < ctx.numSlots; ++i)
    {
        const auto& sl = ctx.slots[(size_t) i];
        const int lfo = sl.dst - (int) ModDst::Lfo1Rate;
        if (lfo < 0 || lfo >= kNumLfos)
            continue;
        float v;
        if (sl.src >= (int) ModSrc::Macro1 && sl.src <= (int) ModSrc::Macro4)
            v = cur.get ((P) ((int) P::Macro1 + sl.src - (int) ModSrc::Macro1));
        else if (sl.src == (int) ModSrc::ModWheel) v = ctx.modWheel;
        else if (sl.src == (int) ModSrc::Aftertouch) v = ctx.aftertouch;
        else if (sl.src == (int) ModSrc::PitchBend) v = ctx.pitchBend;
        else continue;
        freeLfoHz[(size_t) lfo] *= std::exp2 (sl.amt * v * 4.f);
    }

    // Energy sweep: the length in bars follows the tempo (4/4).
    const double seconds = cur.geti (P::EnergyMode) == (int) EnergyMode::Time
                             ? (double) cur.get (P::EnergySec)
                             : (double) energyBarsValue (cur.geti (P::EnergyBars)) * 4.0 * 60.0 / bpm;
    ctx.sweepInc = 1.0 / std::max (1.0, seconds * fs);
    ctx.sweepExp = std::exp2 (2.5f * cur.get (P::EnergyCurve));
    ctx.sweepFade = cur.get (P::EnergyFade);
    ctx.sweepDelay = cur.get (P::EnergyDelay);
    // a one-shot always ends at its length: letting go of the key doesn't
    ctx.sweepCut = cur.on (P::EnergyCut) || cur.on (P::EnergyOneShot);
    for (int i = 0; i < numFxSlots; ++i)
        ctx.sweepUsed = ctx.sweepUsed || fxSlots[(size_t) i].src == (int) ModSrc::Sweep;
}

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------

Voice& Engine::allocateVoice (bool& stolen) noexcept
{
    const int limit = std::clamp (cur.geti (P::PolyVoices), 1, kMaxVoices);
    stolen = false;
    for (int i = 0; i < limit; ++i)
        if (! voices[(size_t) i].isActive())
            return voices[(size_t) i];

    stolen = true;
    // Oldest released voice first, then the oldest overall.
    Voice* best = nullptr;
    for (int i = 0; i < limit; ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.isGateOn() && (best == nullptr || v.age < best->age))
            best = &v;
    }
    if (best == nullptr)
        for (int i = 0; i < limit; ++i)
            if (best == nullptr || voices[(size_t) i].age < best->age)
                best = &voices[(size_t) i];
    return *best;
}

bool Engine::handOff (size_t i, size_t from) noexcept
{
    if (! voices[i].isActive() || voices[i].level() < Voice::kQuiet)
        return false;
    for (size_t j = voices.size(); j-- > std::max (from, (size_t) 1);)
        if (j != i && ! voices[j].isActive())
        {
            std::swap (voices[i], voices[j]);
            voices[j].fadeOut();
            return true;
        }
    return false;
}

void Engine::keyOn (int note, float velocity) noexcept
{
    if (arpActive) arp.keyOn (note, velocity);
    else voiceNoteOn (note, velocity);
}

void Engine::keyOff (int note) noexcept
{
    if (arpActive)
    {
        arp.keyOff (note);   // the arp treats the pedal as hold itself
        return;
    }
    if (sustainPedal)
    {
        sustained[(size_t) note] = true;
        return;
    }
    voiceNoteOff (note);
}

// Follows the On switch. Keys held while it flips move across, so turning
// the arp on (or off) mid-chord keeps the chord going.
void Engine::updateArp() noexcept
{
    const bool want = cur.on (P::ArpOn);
    arp.setSettings (ArpSettings::from (cur));
    if (want == arpActive)
        return;

    if (want)
    {
        std::array<int, 32> keys {};
        std::array<float, 32> vels {};
        std::array<bool, 32> pedal {};
        const int n = numHeld;
        std::copy (held.begin(), held.begin() + n, keys.begin());
        std::copy (heldVel.begin(), heldVel.begin() + n, vels.begin());
        for (int i = 0; i < n; ++i)
            pedal[(size_t) i] = sustained[(size_t) keys[(size_t) i]];   // let go, but the pedal holds them
        allNotesOff();
        sustained.fill (false);
        arpActive = true;
        arp.setSustain (sustainPedal);
        for (int i = 0; i < n; ++i)
        {
            arp.keyOn (keys[(size_t) i], vels[(size_t) i]);
            if (pedal[(size_t) i])
                arp.keyOff (keys[(size_t) i]);   // latched by the pedal: gone when it comes up
        }
    }
    else
    {
        std::array<int, Arpeggiator::kMaxKeys> keys {};
        std::array<float, Arpeggiator::kMaxKeys> vels {};
        const int n = arp.numPhysicalKeys();
        for (int i = 0; i < n; ++i)
        {
            keys[(size_t) i] = arp.physicalKey (i);
            vels[(size_t) i] = arp.physicalVelocity (i);
        }
        arp.allOff();
        fireArp();   // releases the arp's note
        arpActive = false;
        if (! kickMode())   // held keys would fire a kick off the grid
            for (int i = 0; i < n; ++i)
                voiceNoteOn (keys[(size_t) i], vels[(size_t) i]);
    }
}

void Engine::fireArp() noexcept
{
    arp.fireDue ([this] (const ArpEvent& e) {
        if (! e.on) voiceNoteOff (e.note);
        else if (e.slideFrom >= 0) voiceSlide (e.slideFrom, e.note, e.velocity);
        else voiceNoteOn (e.note, e.velocity);
    });
}

// A tied arp step: the voice playing `from` moves to `to` without
// retriggering, in every voice mode.
void Engine::voiceSlide (int from, int to, float velocity) noexcept
{
    if (phosMode())
    {
        if (! phos.isGateOn())
        {
            voiceNoteOff (from);
            voiceNoteOn (to, velocity);
            return;
        }
        const bool accent = velocity >= kPhosAccentVelocity;
        for (int i = numHeld - 1; i >= 0; --i)
            if (held[(size_t) i] == from)
            {
                held[(size_t) i] = to;
                heldAccent[(size_t) i] = accent;
                heldVel[(size_t) i] = velocity;
                break;
            }
        phos.slideTo (to, accent);
        lastNote = to;
        lastPitch = (float) to;
        return;
    }

    Voice* v = nullptr;
    if ((VoiceMode) cur.geti (P::VoiceMode) == VoiceMode::Poly)
    {
        for (auto& x : voices)
            if (x.isGateOn() && x.getNote() == from) { v = &x; break; }
    }
    else if (voices[0].isGateOn() && voices[0].getNote() == from)
    {
        v = &voices[0];
    }
    if (v == nullptr)
    {
        voiceNoteOff (from);
        voiceNoteOn (to, velocity);
        return;
    }

    const bool accent = velocity >= 100.f / 127.f;
    for (int i = numHeld - 1; i >= 0; --i)
        if (held[(size_t) i] == from)
        {
            held[(size_t) i] = to;
            heldAccent[(size_t) i] = accent;
            heldVel[(size_t) i] = velocity;
            break;
        }
    v->changeNote (to, accent, cur.get (P::Glide) > 0.5f);
    lastNote = to;
    lastPitch = (float) to;
}

void Engine::voiceNoteOn (int note, float velocity) noexcept
{
    const auto mode = (VoiceMode) cur.geti (P::VoiceMode);
    const bool accent = velocity >= (phosMode() ? kPhosAccentVelocity : 100.f / 127.f);
    const bool glideAlways = cur.geti (P::GlideMode) == 0;
    const bool hasGlide = cur.get (P::Glide) > 0.5f;
    const uint64_t seed = ++noteSeed * 2654435761u;

    // remember for mono stack; a note that is already down (a doubled
    // note-on) moves to the top instead of going on twice, or its one
    // note-off would leave the copy holding the voice
    dropHeld (note);
    if (numHeld < (int) held.size())
    {
        held[(size_t) numHeld] = note;
        heldAccent[(size_t) numHeld] = accent;
        heldVel[(size_t) numHeld] = velocity;
        ++numHeld;
    }
    sustained[(size_t) note] = false;

    if (qualityPending)
        return;   // the voices are fading out for a quality change; the note is held but doesn't start

    if (kickMode())
    {
        kickNoteOn (note, velocity);
        lastNote = note;
        lastPitch = (float) note;
        return;
    }
    if (phosMode())
    {
        phosNoteOn (note, velocity);
        lastNote = note;
        lastPitch = (float) note;
        return;
    }

    if (mode == VoiceMode::Poly)
    {
        bool stolen = false;
        Voice& v = allocateVoice (stolen);
        const auto slot = (size_t) (&v - voices.data());
        handOff (slot, (size_t) std::clamp (cur.geti (P::PolyVoices), 1, kMaxVoices));
        const float glideFrom = hasGlide && glideAlways && lastPitch >= 0.f ? lastPitch : -1.f;
        v.start (note, velocity, accent, v.isActive(), glideFrom, ctx, seed);
        v.age = ++ageCounter;
        leadVoice = (int) (&v - voices.data());
    }
    else
    {
        Voice& v = voices[0];
        for (size_t i = 1; i < voices.size(); ++i)
            if (voices[i].isActive()) voices[i].release();

        const bool overlapping = v.isGateOn() && numHeld > 1;
        const bool doGlide = hasGlide && (glideAlways || overlapping);
        if (mode == VoiceMode::Legato && overlapping)
        {
            v.changeNote (note, accent, doGlide);
        }
        else
        {
            const float from = doGlide && v.isActive() ? v.currentPitchSemis() : -1.f;
            handOff (0, 1);
            v.start (note, velocity, accent, v.isActive(), from, ctx, seed);
        }
        v.age = ++ageCounter;
        leadVoice = 0;
    }
    lastNote = note;
    lastPitch = (float) note;
}

void Engine::dropHeld (int note) noexcept
{
    int idx = -1;
    for (int i = numHeld - 1; i >= 0; --i)
        if (held[(size_t) i] == note) { idx = i; break; }
    if (idx < 0)
        return;
    for (int i = idx; i < numHeld - 1; ++i)
    {
        held[(size_t) i] = held[(size_t) i + 1];
        heldAccent[(size_t) i] = heldAccent[(size_t) i + 1];
        heldVel[(size_t) i] = heldVel[(size_t) i + 1];
    }
    --numHeld;
}

void Engine::voiceNoteOff (int note) noexcept
{
    dropHeld (note);
    if (kickMode())
        return;   // kicks are one-shots
    if (phosMode())
    {
        // like the original in External mode: a held note under the released
        // one takes over with a slide, the last one up ends the note
        if (phos.isGateOn() && phos.getNote() == note)
        {
            if (numHeld > 0)
            {
                phos.slideTo (held[(size_t) numHeld - 1], heldAccent[(size_t) numHeld - 1]);
                lastPitch = (float) held[(size_t) numHeld - 1];
            }
            else
            {
                phos.release();
                phosModEnv.noteOff();
            }
        }
        return;
    }
    if (cur.on (P::EnergyOneShot))
        return;   // so is a One Shot sound: it plays its whole length and stops there

    // Poly releases every voice on the note. Mono and legato play on voice 0,
    // but a note held through a switch from poly can sit on any other voice.
    const auto mode = (VoiceMode) cur.geti (P::VoiceMode);
    for (size_t i = mode == VoiceMode::Poly ? 0 : 1; i < voices.size(); ++i)
        if (voices[i].isGateOn() && voices[i].getNote() == note)
            voices[i].release();
    if (mode == VoiceMode::Poly)
        return;

    Voice& v = voices[0];
    if (v.getNote() != note || ! v.isGateOn())
        return;
    if (numHeld > 0)
    {
        // fall back to the previous held note (trill / 303 style)
        const int prev = held[(size_t) numHeld - 1];
        const bool hasGlide = cur.get (P::Glide) > 0.5f;
        if (mode == VoiceMode::Legato)
            v.changeNote (prev, heldAccent[(size_t) numHeld - 1], hasGlide);
        else
        {
            const float from = hasGlide ? v.currentPitchSemis() : -1.f;
            handOff (0, 1);
            v.start (prev, heldVel[(size_t) numHeld - 1], heldAccent[(size_t) numHeld - 1], v.isActive(), from, ctx, ++noteSeed * 2654435761u);
        }
        lastPitch = (float) prev;
    }
    else
    {
        v.release();
    }
}

// A new hit takes the quieter kick and fades the other one out. Hits within
// a millisecond of each other (a chord, re-sent held notes) are one hit.
void Engine::kickNoteOn (int note, float velocity) noexcept
{
    auto& last = kicks[(size_t) lastKick];
    if (last.isActive() && last.samplesSinceTrigger() < (int) (0.001 * fs))
    {
        last.raiseVelocity (velocity);
        return;
    }
    const int next = kicks[0].level() <= kicks[1].level() ? 0 : 1;
    kicks[(size_t) (1 - next)].choke();
    kicks[(size_t) next].trigger (note, velocity, ctx);
    lastKick = next;
}

// A note on the 303: a second key down while one is held slides to it (the
// envelopes run on), anything else starts the envelopes again.
void Engine::phosNoteOn (int note, float velocity) noexcept
{
    const bool accent = velocity >= kPhosAccentVelocity;
    if (phos.isGateOn() && numHeld > 1)
    {
        phos.slideTo (note, accent);
        return;
    }
    phos.trigger (note, accent, PhosSettings::from (cur));
    phosVelocity = velocity;
    phosRandom = phosRng.bipolar();
    phosModEnv.set (cur.get (P::MenvA), cur.get (P::MenvD), cur.get (P::MenvS), cur.get (P::MenvR));
    phosModEnv.noteOn();
    for (int i = 0; i < kNumLfos; ++i)
        if (cur.on (lfoParam (i, 4)))
            globalLfos[(size_t) i].reset (cur.get (lfoParam (i, 5)));
}

std::array<float, (size_t) ModSrc::Count> Engine::phosSources (int sampleInBlock) const noexcept
{
    std::array<float, (size_t) ModSrc::Count> src {};
    src[(size_t) ModSrc::AmpEnv] = phos.ampEnv();
    src[(size_t) ModSrc::FilterEnv] = phos.filterEnv();
    src[(size_t) ModSrc::ModEnv] = phosModEnv.value();
    for (int i = 0; i < kNumLfos; ++i)
        src[(size_t) ModSrc::Lfo1 + (size_t) i] = ctx.globalLfo[(size_t) i] != nullptr ? ctx.globalLfo[(size_t) i][sampleInBlock] : 0.f;
    src[(size_t) ModSrc::Velocity] = phosVelocity;
    src[(size_t) ModSrc::Note] = ((float) phos.getNote() - 60.f) / 60.f;
    src[(size_t) ModSrc::ModWheel] = ctx.modWheel;
    src[(size_t) ModSrc::Aftertouch] = ctx.aftertouch;
    src[(size_t) ModSrc::PitchBend] = ctx.pitchBend;
    src[(size_t) ModSrc::Random] = phosRandom;
    for (int m = 0; m < 4; ++m)
        src[(size_t) ModSrc::Macro1 + (size_t) m] = cur.get ((P) ((int) P::Macro1 + m));
    return src;
}

// sampleInBlock: where the chunk starts in the block, which is where the
// free LFOs' buffers are read.
PhosMods Engine::phosMods (int sampleInBlock) const noexcept
{
    PhosMods m;
    m.wheel = ctx.modWheel;
    m.pitch = ctx.pitchBend * (float) cur.geti (P::BendRange);
    if (ctx.numSlots == 0)
        return m;
    const auto src = phosSources (sampleInBlock);
    for (int s = 0; s < ctx.numSlots; ++s)
    {
        const auto& slot = ctx.slots[(size_t) s];
        const float v = slot.amt * src[(size_t) slot.src];
        switch ((ModDst) slot.dst)
        {
            case ModDst::PhosCutoff: m.cutoff += v; break;
            case ModDst::PhosReso:   m.reso += v; break;
            case ModDst::PhosEnvMod: m.envmod += v; break;
            case ModDst::PhosDecay:  m.decay += v; break;
            case ModDst::PhosAccent: m.accent += v; break;
            case ModDst::PhosTune:   m.pitch += v * 24.f; break;
            case ModDst::PitchAll:   m.pitch += v * 24.f; break;
            case ModDst::Amp:        m.level *= std::max (0.f, 1.f + v); break;
            default: break;
        }
    }
    return m;
}

// Synth, kick and Phoscyon voices never sound together: a mode change (a
// new patch) releases the synth voices and fades out the kicks and the 303.
void Engine::followEngineMode() noexcept
{
    const int mode = cur.geti (P::EngineMode);
    if (mode == engineMode)
        return;
    if (engineMode >= 0)
    {
        for (auto& v : voices) v.release();
        for (auto& k : kicks) k.choke();
        phos.fadeOut();
        phosModEnv.kill();
    }
    engineMode = mode;
}

void Engine::allNotesOff() noexcept
{
    arp.allOff();
    numHeld = 0;
    for (auto& v : voices)
        v.release();
    phos.release();
    phosModEnv.noteOff();
}

void Engine::handleEvent (const MidiEvent& e) noexcept
{
    // Values the host got wrong must not reach the voices: NaN in a velocity
    // or a bend would poison them for good, and a note outside 0..127 indexes
    // past the sustain table.
    const bool finite = std::isfinite (e.value);
    const bool isNote = e.type == MidiEvent::NoteOn || e.type == MidiEvent::NoteOff;
    if (isNote && (e.number < 0 || e.number > 127))
        return;
    switch (e.type)
    {
        case MidiEvent::NoteOn:
            if (e.value <= 0.f) keyOff (e.number);
            else keyOn (e.number, finite ? std::min (e.value, 1.f) : 0.8f);
            break;
        case MidiEvent::NoteOff: keyOff (e.number); break;
        case MidiEvent::PitchBend: rawBend = finite ? std::clamp (e.value, -1.f, 1.f) : 0.f; break;
        case MidiEvent::ChannelPressure: rawAftertouch = finite ? std::clamp (e.value, 0.f, 1.f) : 0.f; break;
        case MidiEvent::AllNotesOff: allNotesOff(); break;
        case MidiEvent::Controller:
            if (e.number == 1) rawWheel = finite ? std::clamp (e.value, 0.f, 1.f) : 0.f;
            else if (e.number == 64)
            {
                sustainPedal = e.value >= 0.5f;
                arp.setSustain (arpActive && sustainPedal);
                if (! sustainPedal)
                    for (int n = 0; n < 128; ++n)
                        if (sustained[(size_t) n]) { sustained[(size_t) n] = false; voiceNoteOff (n); }
            }
            else if (e.number == 123 || e.number == 120) allNotesOff();
            break;
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void Engine::renderVoices (int offset, int n, bool record) noexcept
{
    for (size_t i = 0; i < voices.size(); ++i)
        if (voices[i].isActive())
            voices[i].render (osL.data(), osR.data(), offset, n, ctx, record && (int) i == leadVoice ? fxSrc.data() : nullptr);
    for (auto& k : kicks)
        if (k.isActive())
            k.render (osL.data(), osR.data(), offset, n, ctx);
    if (! phos.isActive())
    {
        phosModCount = (phosModCount + n) % kPhosModGrid;   // the grid runs on through silence
        return;
    }
    const auto settings = PhosSettings::from (cur);
    for (int i = 0; i < n;)
    {
        if (phosModCount == 0)
            phosModsNow = phosMods (offset + i);
        const int m = std::min (n - i, kPhosModGrid - phosModCount);
        phos.render (osL.data(), osR.data(), offset + i, m, settings, phosModsNow, record ? fxSrc.data() : nullptr);
        for (int k = 0; k < m; ++k)
        {
            phosModEnv.process();
            if (record)
                fxSrc[(size_t) (offset + i + k)][(size_t) ModSrc::ModEnv] = phosModEnv.value();
        }
        phosModCount = (phosModCount + m) % kPhosModGrid;
        i += m;
    }
}

// A voice, kick or the 303 that put out NaN or infinity: the chunk loses
// those samples, the voice that went bad (its filter state is poisoned for
// good) is killed, and the engine plays on.
void Engine::recoverFromNonFinite (int offset, int n) noexcept
{
    const int from = offset * osFactor, to = (offset + n) * osFactor;
    for (float* buf : { osL.data(), osR.data() })
        for (int i = from; i < to; ++i)
            if (! std::isfinite (buf[i]))
            {
                buf[i] = 0.f;
                ++diag.nonFinite;
            }

    bool found = false;
    for (auto& v : voices)
        if (v.isActive() && ! v.isHealthy())
        {
            v.kill();
            found = true;
        }
    if (! found)
    {
        for (auto& k : kicks) k.kill();
        phos.kill();
        phosModEnv.kill();
    }
}

// Quality changes the oversampling, which restarts every voice's filters and
// the decimator: letting the sounding voices go first (3 ms) keeps that
// click out. The switch happens at the first block with nothing sounding.
void Engine::followQuality() noexcept
{
    const int want = wantedFactor();
    if (want == osFactor)
    {
        qualityPending = false;
        return;
    }
    if (activeVoices() > 0)
    {
        qualityPending = true;
        for (auto& v : voices)
            if (v.isActive()) v.fadeOut();
        for (auto& k : kicks)
            k.choke();
        phos.fadeOut();
        return;
    }
    osFactor = want;
    applyQuality();
    qualityPending = false;
}

// What the effects switched on in the patch delay the audio by, at the base
// rate: the Phoscyon limiter's lookahead (3.3 ms), the Phoscyon distortion's
// oversampling filters (~0.5 ms), our distortion's top restoration below
// 60 kHz when it is first (one sample) or its own oversampling filters
// anywhere else (~1 ms). It changes when one of them is switched.
int Engine::effectLatency() const noexcept
{
    const auto atBase = [this] (int osSamples) { return (int) std::lround ((double) osSamples / osFactor); };
    if (phosMode())
    {
        int lat = cur.on (P::PhosLimOn) ? phosFx.limiterLatency() : 0;
        if (cur.on (P::PhosDistOn))
            lat += phosDistFirst (cur) ? atBase (phosFx.distortionOsLatency()) : phosFx.distortionLatency();
        return lat;
    }
    if (! cur.on (P::DistOn)) return 0;
    return distFirst (cur) ? atBase (dist.latencySamples()) : distLate.latencySamples();
}

void Engine::process (float* left, float* right, int numSamples, const MidiEvent* events, int numEvents, const Transport& transport) noexcept
{
    const double bpm = transport.bpm > 0.0 ? transport.bpm : 120.0;

    // After a reset (a patch swap) the first notes must play the new patch,
    // so the smoothed state jumps before any event is handled.
    if (firstBlock)
    {
        cur = target;
        ctx.modWheel = rawWheel;
        ctx.aftertouch = rawAftertouch;
        ctx.pitchBend = rawBend;
        firstBlock = false;
    }
    else
        snapDiscrete();

    // The arp reads this block's division, swing and mode before it lines up
    // with the host's grid.
    arp.setSettings (ArpSettings::from (cur));
    arp.sync (transport.ppq, bpm, transport.playing);

    const double beatsPerSample = bpm / (60.0 * fs);

    int done = 0;
    int ev = 0;
    while (done < numSamples)
    {
        const int blockN = std::min (numSamples - done, maxBlock);
        followQuality();
        std::fill (osL.begin(), osL.begin() + blockN * osFactor, 0.f);
        std::fill (osR.begin(), osR.begin() + blockN * osFactor, 0.f);

        bool fxMod = false;
        int pos = 0;
        while (pos < blockN)
        {
            // events due now
            followEngineMode();
            updateArp();
            while (ev < numEvents && events[ev].offset <= done + pos)
                handleEvent (events[ev++]);
            if (arpActive)
                fireArp();

            int n = std::min (kChunk, blockN - pos);
            if (ev < numEvents)
                n = std::min (n, std::max (1, events[ev].offset - (done + pos)));
            if (arpActive)
                n = std::min (n, arp.samplesToNextEvent());

            smoothParams (n);
            smoothControllers (n);
            prepareContext (bpm);

            // Free-running LFOs. A synced one is locked to the bar while the host
            // plays: checked every 2048 samples, and at once after a start or a
            // seek. (Between checks it runs on its own, so the checks' timing is
            // all the host's block size can change.)
            const bool lockNow = transport.playing && ((done == 0 && pos == 0 && (! lastPlaying || std::abs (transport.ppq - lastPpqEnd) > 0.002))
                                                       || lfoLockIn <= 0);
            if (lockNow)
                lfoLockIn = 2048;
            lfoLockIn -= n;
            for (int i = 0; i < kNumLfos; ++i)
            {
                auto& lfo = globalLfos[(size_t) i];
                auto& buf = globalLfoBuf[(size_t) i];
                if (lockNow && cur.on (lfoParam (i, 2)) && ! cur.on (lfoParam (i, 4)))
                {
                    const double cycles = (transport.ppq + (double) (done + pos) * beatsPerSample) / syncDivBeats (cur.geti (lfoParam (i, 3)))
                                          + (double) cur.get (lfoParam (i, 5));
                    const float want = (float) (cycles - std::floor (cycles));
                    float d = std::abs (want - lfo.phase());
                    d = std::min (d, 1.f - d);
                    if (d > 1.0e-4f)
                        lfo.reset (want);
                }
                const int shape = cur.geti (lfoParam (i, 0));
                for (int k = 0; k < n; ++k)
                    buf[(size_t) (pos + k)] = lfo.process (shape, freeLfoHz[(size_t) i]);
                ctx.globalLfo[(size_t) i] = buf.data();
            }

            // what the effects' matrix slots will read for these samples
            const bool record = numFxSlots > 0 && ! kickMode();
            if (record)
            {
                fxMod = true;
                std::array<float, (size_t) ModSrc::Count> base {};
                if (phosMode())
                    base = phosSources (pos);
                else
                {
                    base[(size_t) ModSrc::ModWheel] = ctx.modWheel;
                    base[(size_t) ModSrc::Aftertouch] = ctx.aftertouch;
                    base[(size_t) ModSrc::PitchBend] = ctx.pitchBend;
                    for (int m = 0; m < 4; ++m)
                        base[(size_t) ModSrc::Macro1 + (size_t) m] = cur.get ((P) ((int) P::Macro1 + m));
                }
                std::fill (fxSrc.begin() + pos, fxSrc.begin() + pos + n, base);
            }

            renderVoices (pos, n, record);
            if (anyNonFinite (osL.data() + pos * osFactor, n * osFactor) || anyNonFinite (osR.data() + pos * osFactor, n * osFactor))
                recoverFromNonFinite (pos, n);
            if (arpActive)
                arp.advance (n);
            pos += n;
        }

        // Effects see the patch plus their matrix modulation, refreshed every
        // kFxChunk samples (the sources are the ones recorded for each sample).
        // The slices count on from block to block, so they sit on the same
        // samples whatever the host's block size.
        constexpr int kFxChunk = 32;
        const auto distFirstRun = [&] (const ParamState& fp, int from, int count) {
            const int o = from * osFactor, m = count * osFactor;
            if (phosMode())
            {
                const bool on = fp.on (P::PhosDistOn) && phosDistFirst (fp);
                if (phosFx.distortionOsActive (on))
                    phosFx.distortionOs (osL.data() + o, osR.data() + o, m, fp, on);
            }
            else
            {
                const bool on = fp.on (P::DistOn) && distFirst (fp);
                if (dist.isActive (on))
                    dist.process (osL.data() + o, osR.data() + o, m, fp.geti (P::DistType), fp.get (P::DistDrive),
                                  fp.get (P::DistTone), fp.get (P::DistMix), fp.get (P::DistLowCut), fp.on (P::DistAuto), on);
            }
        };
        if (fxMod)
        {
            for (int i = 0; i < blockN;)
            {
                if (fxCountOs == 0) modulateFx (i, fxParamsOs);
                const int m = std::min (blockN - i, kFxChunk - fxCountOs);
                distFirstRun (fxParamsOs, i, m);
                fxCountOs = (fxCountOs + m) % kFxChunk;
                i += m;
            }
        }
        else
        {
            fxCountOs = 0;
            distFirstRun (cur, 0, blockN);
        }

        float* l = left + done;
        float* r = right + done;
        decimL.process (osL.data(), l, blockN, osFactor);
        decimR.process (osR.data(), r, blockN, osFactor);
        if (fxMod)
        {
            for (int i = 0; i < blockN;)
            {
                if (fxCount == 0) modulateFx (i, fxParams);
                const int m = std::min (blockN - i, kFxChunk - fxCount);
                processFx (l + i, r + i, m, bpm, fxParams);
                fxCount = (fxCount + m) % kFxChunk;
                i += m;
            }
        }
        else
        {
            fxCount = 0;
            processFx (l, r, blockN, bpm, cur);
        }
        if (fxBad)
        {
            // an effect's state went to NaN: start the chain over rather than stay silent
            resetEffects();
            fxBad = false;
        }
        done += blockN;
    }
    while (ev < numEvents)
        handleEvent (events[ev++]);
    lastPlaying = transport.playing;
    lastPpqEnd = transport.ppq + (double) numSamples * beatsPerSample;
}

namespace
{
    dsp::CompSettings compSettings (const ParamState& p) noexcept
    {
        dsp::CompSettings s;
        s.on = p.on (P::CompOn);
        s.mode = p.geti (P::CompMode);
        s.attackMs = p.get (P::CompAttack);
        s.releaseMs = p.get (P::CompRelease);
        s.mix = p.get (P::CompMix);
        s.outDb = p.get (P::CompOut);
        s.threshDb = p.get (P::CompThresh);
        s.ratio = p.get (P::CompRatio);
        s.kneeDb = p.get (P::CompKnee);
        s.down = p.get (P::CompDown);
        s.up = p.get (P::CompUp);
        s.xLow = p.get (P::MbXLow);
        s.xHigh = p.get (P::MbXHigh);
        for (int b = 0; b < 3; ++b)
        {
            s.downDb[(size_t) b] = p.get (mbDownParam (b));
            s.upDb[(size_t) b] = p.get (mbUpParam (b));
            s.gainDb[(size_t) b] = p.get (mbGainParam (b));
        }
        return s;
    }
} // namespace

void Engine::modulateFx (int sampleInBlock, ParamState& out) noexcept
{
    // Per-voice sources are the newest note's (or the 303's) at this very
    // sample, as the chunk loop recorded them; macros, controllers and
    // free-running LFOs are the same for every voice.
    auto src = fxSrc[(size_t) sampleInBlock];
    for (int i = 0; i < kNumLfos; ++i)
        if (phosMode() || ! cur.on (lfoParam (i, 4)))
            src[(size_t) ModSrc::Lfo1 + (size_t) i] = globalLfoBuf[(size_t) i][(size_t) sampleInBlock];

    constexpr int kFirst = (int) ModDst::DistDrive, kNum = (int) ModDst::Count - kFirst;
    std::array<float, kNum> sum {};
    for (int s = 0; s < numFxSlots; ++s)
        sum[(size_t) (fxSlots[(size_t) s].dst - kFirst)] += fxSlots[(size_t) s].amt * src[(size_t) fxSlots[(size_t) s].src];

    out = cur;
    for (int i = 0; i < kNum; ++i)
    {
        if (sum[(size_t) i] == 0.f || ! isFxTarget ((ModDst) (kFirst + i))) continue;
        const P prm = modTargetParam ((ModDst) (kFirst + i));
        const auto& d = def (prm);
        const float n = std::clamp (d.toNorm (cur.get (prm)) + sum[(size_t) i], 0.f, 1.f);
        out.v[(size_t) prm] = d.toPlain (n);
    }
}

void Engine::processFx (float* l, float* r, int n, double bpm, const ParamState& p) noexcept
{
    if (phosMode())
    {
        const auto chain = phosChain (p);
        for (int fx : chain)
            processFxSlot (fx, fx == chain[0], l, r, n, bpm, p);
    }
    else
    {
        const auto chain = fxChain (p.geti (P::FxOrder));
        for (int fx : chain)
            processFxSlot (fx, fx == chain[0], l, r, n, bpm, p);
    }

    // the patch's own level (set when it is generated, so every patch plays
    // about as loud), then the user's volume. It ramps from the last sample's
    // gain to this one's: a Volume or Level move reaches the end of a block
    // as a step otherwise.
    const float g = dsp::dbToGain (p.get (P::MasterVol) + p.get (P::PatchGain));
    if (! outGainSet)
    {
        outGain = g;
        outGainSet = true;
    }
    const float gStep = (g - outGain) / (float) n;
    const float g0 = outGain;
    float pk = diag.preClipPeak;
    for (int i = 0; i < n; ++i)
    {
        // NaN or infinity would poison the DC blocker for good: count it, drop it, and rebuild the chain after the block
        float inA = l[i], inB = r[i];
        if (! std::isfinite (inA)) { inA = 0.f; ++diag.nonFinite; fxBad = true; }
        if (! std::isfinite (inB)) { inB = 0.f; ++diag.nonFinite; fxBad = true; }
        const float gi = i == n - 1 ? g : g0 + gStep * (float) (i + 1);
        float a = outDc[0].process (inA) * gi;
        float b = outDc[1].process (inB) * gi;
        if (std::isfinite (a)) pk = std::max (pk, std::abs (a));
        if (std::isfinite (b)) pk = std::max (pk, std::abs (b));
        // transparent up to -0.9 dBFS, soft knee above so output never exceeds 0 dBFS
        if (std::abs (a) > 0.9f) a = std::copysign (0.9f + 0.1f * dsp::fastTanh ((std::abs (a) - 0.9f) * 10.f), a);
        if (std::abs (b) > 0.9f) b = std::copysign (0.9f + 0.1f * dsp::fastTanh ((std::abs (b) - 0.9f) * 10.f), b);
        if (! std::isfinite (a)) { a = 0.f; ++diag.nonFinite; fxBad = true; }
        if (! std::isfinite (b)) { b = 0.f; ++diag.nonFinite; fxBad = true; }
        l[i] = a;
        r[i] = b;
    }
    outGain = g;
    diag.preClipPeak = pk;
}

// One effect of the chain. In Phoscyon mode the original's distortion,
// chorus, delay and reverb take our effects' places, and its EQ and limiter
// sit in their own slots. Every effect runs while it is on or fading out
// (`isActive`): a switch fades over 10 ms and a restart begins clean, so no
// old echo or lookahead audio comes back.
void Engine::processFxSlot (int fx, bool first, float* l, float* r, int n, double bpm, const ParamState& p) noexcept
{
    if (phosMode())
    {
        switch (fx)
        {
            case (int) Fx::Distortion:
            {
                const bool on = p.on (P::PhosDistOn) && ! first;
                if (phosFx.distortionActive (on)) phosFx.distortion (l, r, n, p, on);
                return;
            }
            case (int) Fx::Chorus:
                if (const bool on = p.on (P::PhosChoOn); phosFx.chorusActive (on)) phosFx.chorus (l, r, n, p, on);
                return;
            case (int) Fx::Delay:
                if (const bool on = p.on (P::PhosDlyOn); phosFx.delayActive (on)) phosFx.delay (l, r, n, bpm, p, on);
                return;
            case (int) Fx::Reverb:
                if (const bool on = p.on (P::PhosRevOn); phosFx.reverbActive (on)) phosFx.reverb (l, r, n, p, on);
                return;
            case kPhosEq:
                if (const bool on = p.on (P::PhosEqOn); phosFx.eqActive (on)) phosFx.eq (l, r, n, p, on);
                return;
            case kPhosLimiter:
                if (const bool on = p.on (P::PhosLimOn); phosFx.limiterActive (on)) phosFx.limiter (l, r, n, p, on);
                return;
            default:
                break;
        }
    }

    switch ((Fx) fx)
    {
        case Fx::Distortion:
            if (const bool on = p.on (P::DistOn) && ! first; distLate.isActive (on))
                distLate.process (l, r, n, p.geti (P::DistType), p.get (P::DistDrive), p.get (P::DistTone), p.get (P::DistMix),
                                  p.get (P::DistLowCut), p.on (P::DistAuto), on);
            break;
        case Fx::Crush:
            if (const bool on = p.on (P::CrushOn); crush.isActive (on))
                crush.process (l, r, n, p.get (P::CrushBits), p.get (P::CrushRate), p.get (P::CrushMix), on);
            break;
        case Fx::Shift:
            if (const bool on = p.on (P::FshiftOn); fshift.isActive (on))
                fshift.process (l, r, n, p.get (P::FshiftHz), p.get (P::FshiftFb), p.get (P::FshiftSpread), p.get (P::FshiftMix), on);
            break;
        case Fx::Phaser:
            if (const bool on = p.on (P::PhaserOn); phaser.isActive (on))
                phaser.process (l, r, n, p.get (P::PhaserRate), p.get (P::PhaserDepth), p.get (P::PhaserFb), p.get (P::PhaserCenter), p.get (P::PhaserMix), on);
            break;
        case Fx::Chorus:
            if (const bool on = p.on (P::ChorusOn); chorus.isActive (on))
                chorus.process (l, r, n, p.geti (P::ChorusMode) == 1, p.get (P::ChorusRate), p.get (P::ChorusDepth), p.get (P::ChorusFb), p.get (P::ChorusMix), on);
            break;
        case Fx::Compressor:
            if (comp.isActive (p.on (P::CompOn)))
                comp.process (l, r, n, compSettings (p));
            break;
        case Fx::Delay:
            if (const bool on = p.on (P::DelayOn); delay.isActive (on))
            {
                const float ms = p.on (P::DelaySync) ? (float) (60000.0 / bpm) * syncDivBeats (p.geti (P::DelayDiv)) : p.get (P::DelayMs);
                delay.process (l, r, n, std::min (ms, 2000.f), p.get (P::DelayFb), p.on (P::DelayPing), p.get (P::DelayLowCut), p.get (P::DelayHighCut), p.get (P::DelayMix), on);
            }
            break;
        case Fx::Reverb:
            if (const bool on = p.on (P::ReverbOn); reverb.isActive (on))
                reverb.process (l, r, n, p.get (P::ReverbSize), p.get (P::ReverbDecay), p.get (P::ReverbDamp), p.get (P::ReverbPre), p.get (P::ReverbMix), on);
            break;
        case Fx::Width:   // last by default, so Low Mono also cleans the delay and reverb lows
            if (width.isActive (p.on (P::WidthOn)))
                width.process (l, r, n, p.on (P::WidthOn), p.get (P::WidthAmount), p.get (P::WidthStereo), p.get (P::WidthTime), p.get (P::WidthMonoHz));
            break;
        case Fx::Count:
            break;
    }
}

} // namespace psy
