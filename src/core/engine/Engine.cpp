#include "Engine.h"

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
    fshift.prepare (f);
    phaser.prepare (f);
    chorus.prepare (f);
    comp.prepare (f);
    delay.prepare (f);
    reverb.prepare (f);
    for (auto& d : outDc) d.prepare (f);

    osFactor = factorForQuality (target.geti (P::Quality));
    applyQuality();
    reset();
    cur = target;  // notes in the very first block must see the real patch
    firstBlock = false;
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
    for (auto& v : voices) v.kill();
    for (auto& k : kicks) k.kill();
    engineMode = -1;
    numHeld = 0;
    sustained.fill (false);
    sustainPedal = false;
    arp.reset();
    arpActive = false;
    dist.reset();
    crush.reset();
    fshift.reset();
    phaser.reset();
    chorus.reset();
    comp.reset();
    delay.reset();
    reverb.reset();
    for (auto& d : outDc) d.reset();
    decimL.reset();
    decimR.reset();
    firstBlock = true;
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
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const int src = cur.geti (modSrcParam (s));
        const int dst = cur.geti (modDstParam (s));
        const float amt = cur.get (modAmtParam (s));
        if (src > 0 && dst > 0 && amt != 0.f)
            ctx.slots[(size_t) ctx.numSlots++] = { src, dst, amt };
    }
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

    // remember for mono stack
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
    }
    lastNote = note;
    lastPitch = (float) note;
}

void Engine::voiceNoteOff (int note) noexcept
{
    // drop from stack
    int idx = -1;
    for (int i = numHeld - 1; i >= 0; --i)
        if (held[(size_t) i] == note) { idx = i; break; }
    if (idx >= 0)
    {
        for (int i = idx; i < numHeld - 1; ++i)
        {
            held[(size_t) i] = held[(size_t) i + 1];
            heldAccent[(size_t) i] = heldAccent[(size_t) i + 1];
        }
        --numHeld;
    }
    if (kickMode())
        return;   // kicks are one-shots

    const auto mode = (VoiceMode) cur.geti (P::VoiceMode);
    if (mode == VoiceMode::Poly)
    {
        for (auto& v : voices)
            if (v.isGateOn() && v.getNote() == note)
                v.release();
        return;
    }

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

    const int wantFactor = factorForQuality (target.geti (P::Quality));
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

        if (cur.on (P::DistOn))
            dist.process (osL.data(), osR.data(), blockN * osFactor, cur.geti (P::DistType), cur.get (P::DistDrive),
                          cur.get (P::DistTone), cur.get (P::DistMix));

        float* l = left + done;
        float* r = right + done;
        decimL.process (osL.data(), l, blockN, osFactor);
        decimR.process (osR.data(), r, blockN, osFactor);
        processFx (l, r, blockN, bpm);
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

void Engine::processFx (float* l, float* r, int n, double bpm) noexcept
{
    const auto& p = cur;
    if (p.on (P::CrushOn))
        crush.process (l, r, n, p.get (P::CrushBits), p.get (P::CrushRate), p.get (P::CrushMix));
    if (p.on (P::FshiftOn))
        fshift.process (l, r, n, p.get (P::FshiftHz), p.get (P::FshiftFb), p.get (P::FshiftSpread), p.get (P::FshiftMix));
    if (p.on (P::PhaserOn))
        phaser.process (l, r, n, p.get (P::PhaserRate), p.get (P::PhaserDepth), p.get (P::PhaserFb), p.get (P::PhaserCenter), p.get (P::PhaserMix));
    if (p.on (P::ChorusOn))
        chorus.process (l, r, n, p.geti (P::ChorusMode) == 1, p.get (P::ChorusRate), p.get (P::ChorusDepth), p.get (P::ChorusFb), p.get (P::ChorusMix));
    if (comp.isActive (p.on (P::CompOn)))
        comp.process (l, r, n, compSettings (p));
    if (p.on (P::DelayOn))
    {
        const float ms = p.on (P::DelaySync) ? (float) (60000.0 / bpm) * syncDivBeats (p.geti (P::DelayDiv)) : p.get (P::DelayMs);
        delay.process (l, r, n, std::min (ms, 2000.f), p.get (P::DelayFb), p.on (P::DelayPing), p.get (P::DelayLowCut), p.get (P::DelayHighCut), p.get (P::DelayMix));
    }
    if (p.on (P::ReverbOn))
        reverb.process (l, r, n, p.get (P::ReverbSize), p.get (P::ReverbDecay), p.get (P::ReverbDamp), p.get (P::ReverbPre), p.get (P::ReverbMix));

    const float g = dsp::dbToGain (p.get (P::MasterVol));
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
