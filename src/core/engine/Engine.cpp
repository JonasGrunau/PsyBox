#include "Engine.h"

#include <algorithm>
#include <cstring>

namespace psy
{

namespace
{
    constexpr int kChunk = 16;  // control-rate granularity for parameter smoothing

    int factorForQuality (int q) noexcept { return q == 0 ? 1 : (q == 1 ? 2 : 4); }
    float harmonicLimitFor (int factor) noexcept { return factor == 1 ? 0.45f : (factor == 2 ? 0.62f : 0.8f); }
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

    for (auto& v : voices)
        v.prepare ((float) fs, (float) fs * 4.f);
    for (auto& k : kicks)
        k.prepare ((float) fs, (float) fs * 2.f);

    for (int i = 0; i < kNumLfos; ++i)
    {
        globalLfos[(size_t) i].setSampleRate ((float) fs);
        globalLfos[(size_t) i].seed (1000u + (uint64_t) i);
    }

    arp.prepare (fs);

    const float f = (float) fs;
    distLate.prepare (f);
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
    ctx.baseRate = (float) fs;
    ctx.osRate = osRate;
    ctx.osFactor = osFactor;
    ctx.harmonicLimit = harmonicLimitFor (osFactor);
    dist.prepare (osRate);  // runs before decimation, so it is oversampled for free
    decimL.reset();
    decimR.reset();
}

void Engine::reset()
{
    leadVoice = -1;
    for (auto& v : voices) v.kill();
    for (auto& k : kicks) k.kill();
    engineMode = -1;
    numHeld = 0;
    sustained.fill (false);
    sustainPedal = false;
    arp.reset();
    arpActive = false;
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
    firstBlock = true;
}

void Engine::getModView (ModView& out) const noexcept
{
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
    return n;
}

void Engine::smoothParams (int numSamples) noexcept
{
    const auto& defs = paramDefs();
    if (firstBlock)
    {
        cur = target;
        firstBlock = false;
        return;
    }
    const float a = 1.f - std::exp (-(float) numSamples / (0.012f * (float) fs));
    for (size_t i = 0; i < (size_t) kNumParams; ++i)
    {
        if (defs[i].isDiscrete())
        {
            cur.v[i] = target.v[i];
        }
        else if (defs[i].mapping == Mapping::Exp)
        {
            // smooth frequencies in the log domain so sweeps sound even
            const float lc = std::log (cur.v[i]), lt = std::log (target.v[i]);
            cur.v[i] = std::exp (lc + (lt - lc) * a);
        }
        else
        {
            cur.v[i] += (target.v[i] - cur.v[i]) * a;
        }
    }
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
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const int src = cur.geti (modSrcParam (s));
        const int dst = cur.geti (modDstParam (s));
        const float amt = cur.get (modAmtParam (s));
        if (src <= 0 || dst <= 0 || amt == 0.f || src >= (int) ModSrc::Count || dst >= (int) ModDst::Count)
            continue;
        if (isFxTarget ((ModDst) dst))
            fxSlots[(size_t) numFxSlots++] = { src, dst, amt };
        else
            ctx.slots[(size_t) ctx.numSlots++] = { src, dst, amt };
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
        std::array<bool, 32> accents {};
        const int n = numHeld;
        std::copy (held.begin(), held.begin() + n, keys.begin());
        std::copy (heldAccent.begin(), heldAccent.begin() + n, accents.begin());
        allNotesOff();
        sustained.fill (false);
        arpActive = true;
        arp.setSustain (sustainPedal);
        for (int i = 0; i < n; ++i)
            arp.keyOn (keys[(size_t) i], accents[(size_t) i] ? 1.f : 0.8f);
    }
    else
    {
        std::array<int, Arpeggiator::kMaxKeys> keys {};
        const int n = arp.numPhysicalKeys();
        for (int i = 0; i < n; ++i)
            keys[(size_t) i] = arp.physicalKey (i);
        arp.allOff();
        fireArp();   // releases the arp's note
        arpActive = false;
        if (! kickMode())   // held keys would fire a kick off the grid
            for (int i = 0; i < n; ++i)
                voiceNoteOn (keys[(size_t) i], 0.8f);
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
            break;
        }
    v->changeNote (to, accent, cur.get (P::Glide) > 0.5f);
    lastNote = to;
    lastPitch = (float) to;
}

void Engine::voiceNoteOn (int note, float velocity) noexcept
{
    const auto mode = (VoiceMode) cur.geti (P::VoiceMode);
    const bool accent = velocity >= 100.f / 127.f;
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
        ++numHeld;
    }
    sustained[(size_t) note] = false;

    if (kickMode())
    {
        kickNoteOn (note, velocity);
        lastNote = note;
        lastPitch = (float) note;
        return;
    }

    if (mode == VoiceMode::Poly)
    {
        bool stolen = false;
        Voice& v = allocateVoice (stolen);
        const float glideFrom = hasGlide && glideAlways && lastPitch >= 0.f ? lastPitch : -1.f;
        v.start (note, velocity, accent, true, stolen || v.isActive(), glideFrom, ctx, seed);
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
            v.start (note, velocity, accent, true, v.isActive(), from, ctx, seed);
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
    }
    --numHeld;
}

void Engine::voiceNoteOff (int note) noexcept
{
    dropHeld (note);
    if (kickMode())
        return;   // kicks are one-shots
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
            v.start (prev, 0.8f, heldAccent[(size_t) numHeld - 1], true, true,
                     hasGlide ? v.currentPitchSemis() : -1.f, ctx, ++noteSeed * 2654435761u);
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

// Synth and kick voices never sound together: a mode change (a new patch)
// releases the synth voices and fades out the kicks.
void Engine::followEngineMode() noexcept
{
    const int mode = cur.geti (P::EngineMode);
    if (mode == engineMode)
        return;
    if (engineMode >= 0)
    {
        for (auto& v : voices) v.release();
        for (auto& k : kicks) k.choke();
    }
    engineMode = mode;
}

void Engine::allNotesOff() noexcept
{
    arp.allOff();
    numHeld = 0;
    for (auto& v : voices)
        v.release();
}

void Engine::handleEvent (const MidiEvent& e) noexcept
{
    switch (e.type)
    {
        case MidiEvent::NoteOn:
            if (e.value <= 0.f) keyOff (e.number);
            else keyOn (e.number, e.value);
            break;
        case MidiEvent::NoteOff: keyOff (e.number); break;
        case MidiEvent::PitchBend: ctx.pitchBend = e.value; break;
        case MidiEvent::ChannelPressure: ctx.aftertouch = e.value; break;
        case MidiEvent::AllNotesOff: allNotesOff(); break;
        case MidiEvent::Controller:
            if (e.number == 1) ctx.modWheel = e.value;
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

void Engine::renderVoices (int offset, int n) noexcept
{
    for (auto& v : voices)
        if (v.isActive())
            v.render (osL.data(), osR.data(), offset, n, ctx);
    for (auto& k : kicks)
        if (k.isActive())
            k.render (osL.data(), osR.data(), offset, n, ctx);
}

void Engine::process (float* left, float* right, int numSamples, const MidiEvent* events, int numEvents, const Transport& transport) noexcept
{
    const double bpm = transport.bpm > 0.0 ? transport.bpm : 120.0;
    arp.sync (transport.ppq, bpm, transport.playing);

    // After a reset (a patch swap) the first notes must play the new patch,
    // so the smoothed state jumps before any event is handled.
    if (firstBlock)
    {
        cur = target;
        firstBlock = false;
    }

    const int wantFactor = wantedFactor();
    if (wantFactor != osFactor)
    {
        osFactor = wantFactor;
        applyQuality();
    }

    int done = 0;
    int ev = 0;
    while (done < numSamples)
    {
        const int blockN = std::min (numSamples - done, maxBlock);
        std::fill (osL.begin(), osL.begin() + blockN * osFactor, 0.f);
        std::fill (osR.begin(), osR.begin() + blockN * osFactor, 0.f);

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
            prepareContext (bpm);

            // free-running LFOs
            for (int i = 0; i < kNumLfos; ++i)
            {
                auto& buf = globalLfoBuf[(size_t) i];
                const int shape = cur.geti (lfoParam (i, 0));
                for (int k = 0; k < n; ++k)
                    buf[(size_t) (pos + k)] = globalLfos[(size_t) i].process (shape, ctx.lfoHz[(size_t) i]);
                ctx.globalLfo[(size_t) i] = buf.data();
            }

            renderVoices (pos, n);
            if (arpActive)
                arp.advance (n);
            pos += n;
        }

        // Effects see the patch plus their matrix modulation, refreshed every
        // kFxChunk samples (the free LFOs are read where each chunk starts).
        const bool fxMod = numFxSlots > 0 && ! kickMode();
        if (fxMod) modulateFx (0);
        const ParamState& fp = fxMod ? fxParams : cur;
        if (fp.on (P::DistOn) && distFirst (fp))
            dist.process (osL.data(), osR.data(), blockN * osFactor, fp.geti (P::DistType), fp.get (P::DistDrive),
                          fp.get (P::DistTone), fp.get (P::DistMix), fp.get (P::DistLowCut), fp.on (P::DistAuto));

        float* l = left + done;
        float* r = right + done;
        decimL.process (osL.data(), l, blockN, osFactor);
        decimR.process (osR.data(), r, blockN, osFactor);
        if (fxMod)
        {
            constexpr int kFxChunk = 32;
            for (int i = 0; i < blockN; i += kFxChunk)
            {
                modulateFx (i);
                processFx (l + i, r + i, std::min (kFxChunk, blockN - i), bpm, fxParams);
            }
        }
        else
        {
            processFx (l, r, blockN, bpm, cur);
        }
        done += blockN;
    }
    while (ev < numEvents)
        handleEvent (events[ev++]);
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

void Engine::modulateFx (int sampleInBlock) noexcept
{
    // Per-voice sources come from the newest note while it sounds; macros,
    // controllers and free-running LFOs are the same for every voice.
    std::array<float, (size_t) ModSrc::Count> src {};
    if (leadVoice >= 0 && voices[(size_t) leadVoice].isActive())
        src = voices[(size_t) leadVoice].sources();
    for (int i = 0; i < kNumLfos; ++i)
        if (! cur.on (lfoParam (i, 4)))
            src[(size_t) ModSrc::Lfo1 + (size_t) i] = globalLfoBuf[(size_t) i][(size_t) sampleInBlock];
    src[(size_t) ModSrc::ModWheel] = ctx.modWheel;
    src[(size_t) ModSrc::Aftertouch] = ctx.aftertouch;
    src[(size_t) ModSrc::PitchBend] = ctx.pitchBend;
    for (int m = 0; m < 4; ++m)
        src[(size_t) ModSrc::Macro1 + (size_t) m] = cur.get ((P) ((int) P::Macro1 + m));

    constexpr int kFirst = (int) ModDst::DistDrive, kNum = (int) ModDst::Count - kFirst;
    std::array<float, kNum> sum {};
    for (int s = 0; s < numFxSlots; ++s)
        sum[(size_t) (fxSlots[(size_t) s].dst - kFirst)] += fxSlots[(size_t) s].amt * src[(size_t) fxSlots[(size_t) s].src];

    fxParams = cur;
    for (int i = 0; i < kNum; ++i)
    {
        if (sum[(size_t) i] == 0.f) continue;
        const P prm = modTargetParam ((ModDst) (kFirst + i));
        const auto& d = def (prm);
        const float n = std::clamp (d.toNorm (cur.get (prm)) + sum[(size_t) i], 0.f, 1.f);
        fxParams.v[(size_t) prm] = d.toPlain (n);
    }
}

void Engine::processFx (float* l, float* r, int n, double bpm, const ParamState& p) noexcept
{
    const auto chain = fxChain (p.geti (P::FxOrder));
    for (int fx : chain)
        switch ((Fx) fx)
        {
            case Fx::Distortion:
                if (p.on (P::DistOn) && chain[0] != (int) Fx::Distortion)
                    distLate.process (l, r, n, p.geti (P::DistType), p.get (P::DistDrive), p.get (P::DistTone), p.get (P::DistMix),
                                      p.get (P::DistLowCut), p.on (P::DistAuto));
                break;
            case Fx::Crush:
                if (p.on (P::CrushOn))
                    crush.process (l, r, n, p.get (P::CrushBits), p.get (P::CrushRate), p.get (P::CrushMix));
                break;
            case Fx::Shift:
                if (p.on (P::FshiftOn))
                    fshift.process (l, r, n, p.get (P::FshiftHz), p.get (P::FshiftFb), p.get (P::FshiftSpread), p.get (P::FshiftMix));
                break;
            case Fx::Phaser:
                if (p.on (P::PhaserOn))
                    phaser.process (l, r, n, p.get (P::PhaserRate), p.get (P::PhaserDepth), p.get (P::PhaserFb), p.get (P::PhaserCenter), p.get (P::PhaserMix));
                break;
            case Fx::Chorus:
                if (p.on (P::ChorusOn))
                    chorus.process (l, r, n, p.geti (P::ChorusMode) == 1, p.get (P::ChorusRate), p.get (P::ChorusDepth), p.get (P::ChorusFb), p.get (P::ChorusMix));
                break;
            case Fx::Compressor:
                if (comp.isActive (p.on (P::CompOn)))
                    comp.process (l, r, n, compSettings (p));
                break;
            case Fx::Delay:
                if (p.on (P::DelayOn))
                {
                    const float ms = p.on (P::DelaySync) ? (float) (60000.0 / bpm) * syncDivBeats (p.geti (P::DelayDiv)) : p.get (P::DelayMs);
                    delay.process (l, r, n, std::min (ms, 2000.f), p.get (P::DelayFb), p.on (P::DelayPing), p.get (P::DelayLowCut), p.get (P::DelayHighCut), p.get (P::DelayMix));
                }
                break;
            case Fx::Reverb:
                if (p.on (P::ReverbOn))
                    reverb.process (l, r, n, p.get (P::ReverbSize), p.get (P::ReverbDecay), p.get (P::ReverbDamp), p.get (P::ReverbPre), p.get (P::ReverbMix));
                break;
            case Fx::Width:   // last by default, so Low Mono also cleans the delay and reverb lows
                if (width.isActive (p.on (P::WidthOn)))
                    width.process (l, r, n, p.on (P::WidthOn), p.get (P::WidthAmount), p.get (P::WidthStereo), p.get (P::WidthTime), p.get (P::WidthMonoHz));
                break;
            case Fx::Count:
                break;
        }

    // the patch's own level (set when it is generated, so every patch plays
    // about as loud), then the user's volume
    const float g = dsp::dbToGain (p.get (P::MasterVol) + p.get (P::PatchGain));
    float pk = 0.f;
    for (int i = 0; i < n; ++i)
    {
        float a = outDc[0].process (l[i]) * g;
        float b = outDc[1].process (r[i]) * g;
        // transparent up to -0.9 dBFS, soft knee above so output never exceeds 0 dBFS
        if (std::abs (a) > 0.9f) a = std::copysign (0.9f + 0.1f * dsp::fastTanh ((std::abs (a) - 0.9f) * 10.f), a);
        if (std::abs (b) > 0.9f) b = std::copysign (0.9f + 0.1f * dsp::fastTanh ((std::abs (b) - 0.9f) * 10.f), b);
        if (! std::isfinite (a)) a = 0.f;
        if (! std::isfinite (b)) b = 0.f;
        l[i] = a;
        r[i] = b;
        pk = std::max (pk, std::max (std::abs (a), std::abs (b)));
    }
    peak = pk;
}

} // namespace psy
