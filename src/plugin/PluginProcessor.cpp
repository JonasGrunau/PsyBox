#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "generator/Loudness.h"

using namespace psy;

namespace
{
    constexpr int kStateVersion = 1;

    juce::NormalisableRange<float> rangeFor (const ParamDef& d)
    {
        juce::NormalisableRange<float> r (
            d.min, d.max,
            [&d] (float, float, float n) { return d.toPlain (n); },
            [&d] (float, float, float v) { return d.toNorm (v); },
            [&d] (float, float, float v) { return d.snap (v); });
        return r;
    }

    // A continuous parameter that gives the host back exactly the normalised
    // value it set. AudioParameterFloat keeps the plain value instead, and
    // toPlain, snap, toNorm comes back a few ulps off (auval warns), or never
    // reaches an end at all where a law can't (PhosC13's Inverse at 0).
    class ExactParameter final : public juce::RangedAudioParameter
    {
    public:
        ExactParameter (const juce::ParameterID& id, const juce::String& paramName, const ParamDef& d, bool automatable)
            : RangedAudioParameter (id, paramName, juce::AudioProcessorParameterWithIDAttributes().withAutomatable (automatable)),
              pd (d), range (rangeFor (d)), defaultNorm (d.toNorm (d.snap (d.def))), norm (defaultNorm)
        {
        }

        const juce::NormalisableRange<float>& getNormalisableRange() const override { return range; }
        float getValue() const override { return norm.load (std::memory_order_relaxed); }
        void setValue (float v) override { norm.store (juce::jlimit (0.f, 1.f, v), std::memory_order_relaxed); }
        float getDefaultValue() const override { return defaultNorm; }
        juce::String getText (float v, int) const override { return juce::String (pd.format (convertFrom0to1 (v))); }
        float getValueForText (const juce::String& s) const override { return convertTo0to1 (pd.parse (s.toStdString())); }

    private:
        const ParamDef& pd;
        const juce::NormalisableRange<float> range;
        const float defaultNorm;
        std::atomic<float> norm;
    };

    // Accepts state saved under the working title "PsySynth" and renames its tag to ours.
    bool adoptStateTag (juce::XmlElement* xml, const juce::Identifier& type)
    {
        if (xml == nullptr) return false;
        if (xml->hasTagName ("PsySynth")) xml->setTagName (type);
        return xml->hasTagName (type);
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PsyBoxProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& d : paramDefs())
    {
        const juce::ParameterID id { d.id, kStateVersion };
        const juce::String name = juce::String (d.group) + " " + d.name;
        if (d.mapping == Mapping::Choice)
        {
            juce::StringArray items;
            for (const auto& c : *d.choices)
                items.add (c);
            // The engine mode belongs to the category, not to host automation.
            const auto attrs = juce::AudioParameterChoiceAttributes().withAutomatable (std::string_view (d.id) != "engine_mode");
            layout.add (std::make_unique<juce::AudioParameterChoice> (id, name, items, (int) d.def, attrs));
        }
        else if (d.mapping == Mapping::Toggle)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (id, name, d.def > 0.5f));
        }
        else
        {
            // The effect order is the patch's layout, not something to automate.
            layout.add (std::make_unique<ExactParameter> (id, name, d, std::string_view (d.id) != "fx_order"));
        }
    }
    return layout;
}

PsyBoxProcessor::PsyBoxProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PsyBox", createLayout())
{
    resetMacroNames();
    const auto& defs = paramDefs();
    for (size_t i = 0; i < defs.size(); ++i)
    {
        params[i] = apvts.getParameter (defs[i].id);
        rawValues[i] = apvts.getRawParameterValue (defs[i].id);
        jassert (params[i] != nullptr && rawValues[i] != nullptr);
    }
    midiEvents.reserve (1024);
    seed = (uint32_t) random.nextInt();
    pushHistory();
}

bool PsyBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PsyBoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setOffline (isNonRealtime());   // a render reports the Render quality's latency from the start
    engine.setParams (currentState());
    engine.prepare (sampleRate, samplesPerBlock);
    reportedLatency = engine.latencySamples();
    setLatencySamples (reportedLatency);
    swap = Swap::None;
    seenEpoch = patchEpoch.load();
    maxHold = (int) sampleRate;
    fadeOutLen = std::max (1, (int) std::lround (sampleRate * 0.010));
    fadeInLen = std::max (1, (int) std::lround (sampleRate * (128.0 / 48000.0)));
    heldVelocity.fill (0.f);
    pedalHeld.fill (0.f);
    pressedInFade.fill (false);
    sustainDown = false;
    wasBypassed = false;
}

ParamState PsyBoxProcessor::currentState() const
{
    ParamState s;
    for (size_t i = 0; i < (size_t) kNumParams; ++i)
        s.v[i] = rawValues[i]->load (std::memory_order_relaxed);
    return s;
}

void PsyBoxProcessor::trackKeys (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
    {
        const auto k = (size_t) m.getNoteNumber();
        lastPlayedNote.store (m.getNoteNumber(), std::memory_order_relaxed);
        heldVelocity[k] = std::max (0.01f, m.getFloatVelocity());
        pedalHeld[k] = 0.f;
    }
    else if (m.isNoteOff())
    {
        const auto k = (size_t) m.getNoteNumber();
        if (sustainDown && heldVelocity[k] > 0.f)
            pedalHeld[k] = heldVelocity[k];
        heldVelocity[k] = 0.f;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        heldVelocity.fill (0.f);
        pedalHeld.fill (0.f);
        pressedInFade.fill (false);
    }
    else if (m.isSustainPedalOn())   // CC 64 >= 64, the engine's threshold
    {
        sustainDown = true;
    }
    else if (m.isSustainPedalOff())
    {
        sustainDown = false;
        pedalHeld.fill (0.f);
    }
}

void PsyBoxProcessor::startFadeOut() noexcept
{
    // from where the sound is: a fade-in still running starts the fade-out lower
    fadePos = swap == Swap::Restart ? (int) ((1.f - (float) fadePos / (float) fadeInLen) * (float) fadeOutLen) : 0;
    swap = Swap::FadingOut;   // keep the old values for the fade
}

void PsyBoxProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    for (const auto meta : midi)
        trackKeys (meta.getMessage());
    wasBypassed = true;
}

void PsyBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0)
        return;

    // Back from a host bypass: the engine still holds whatever it held when
    // the bypass began, so it restarts like after a swap (already silent)
    // and the keys down now come back.
    if (wasBypassed)
    {
        wasBypassed = false;
        swap = Swap::FadingOut;
        fadePos = fadeOutLen;
    }

    if (swap != Swap::FadingOut && patchEpoch.load (std::memory_order_acquire) != seenEpoch)
        startFadeOut();
    if (swap != Swap::FadingOut)
    {
        for (size_t i = 0; i < (size_t) kNumParams; ++i)
            incoming.v[i] = rawValues[i]->load (std::memory_order_relaxed);
        // A change that opened while these were read may have written some of
        // its values already: keep the old ones and fade out on them instead.
        std::atomic_thread_fence (std::memory_order_acquire);
        if (patchEpoch.load (std::memory_order_relaxed) == seenEpoch)
        {
            audioParams = incoming;
            engine.setParams (audioParams);
        }
        else
        {
            startFadeOut();
        }
    }

    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    // First block after a reset: the keys that were sounding come back
    // (taken before this block's MIDI: a key pressed now brings its own
    // note-on, and replaying it too would start it twice).
    const bool restart = swap == Swap::Restart && fadePos == 0;
    std::array<float, 128> replay {}, replayPedal {};
    const bool replaySustain = restart && sustainDown;
    if (restart)
    {
        replay = heldVelocity;
        replayPedal = pedalHeld;
    }

    midiEvents.clear();
    const size_t forNotes = midiEvents.capacity() / 4;   // controllers never crowd out note events
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (swap == Swap::FadingOut && m.isNoteOn())
            pressedInFade[(size_t) m.getNoteNumber()] = true;
        trackKeys (m);
        MidiEvent e;
        e.offset = juce::jlimit (0, n - 1, meta.samplePosition);
        // CC 120/123 before the other controllers, which they also are
        if (m.isNoteOn()) { e.type = MidiEvent::NoteOn; e.number = m.getNoteNumber(); e.value = m.getFloatVelocity(); }
        else if (m.isNoteOff()) { e.type = MidiEvent::NoteOff; e.number = m.getNoteNumber(); }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { e.type = MidiEvent::AllNotesOff; }
        else if (m.isPitchWheel()) { e.type = MidiEvent::PitchBend; e.value = (float) (m.getPitchWheelValue() - 8192) / 8192.f; }
        else if (m.isController()) { e.type = MidiEvent::Controller; e.number = m.getControllerNumber(); e.value = (float) m.getControllerValue() / 127.f; }
        else if (m.isChannelPressure()) { e.type = MidiEvent::ChannelPressure; e.value = (float) m.getChannelPressureValue() / 127.f; }
        else if (m.isAftertouch()) { e.type = MidiEvent::ChannelPressure; e.value = (float) m.getAfterTouchValue() / 127.f; }
        else continue;
        const bool note = e.type == MidiEvent::NoteOn || e.type == MidiEvent::NoteOff || e.type == MidiEvent::AllNotesOff;
        if (midiEvents.size() + (note ? 0 : forNotes) < midiEvents.capacity())
            midiEvents.push_back (e);
    }

    Transport transport;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            if (const auto b = pos->getBpm(); b && *b > 0.0)
                transport.bpm = *b;
            if (const auto ppq = pos->getPpqPosition())
            {
                transport.ppq = *ppq;
                transport.playing = pos->getIsPlaying();
            }
        }
    const double bpm = transport.bpm;
    lastBpm.store (bpm, std::memory_order_relaxed);

    // The new patch starts clean and what sounded comes back: the pedal, the
    // keys held and the keys the pedal held (let go again, so the engine
    // sustains them). A kick is a one-shot: only keys pressed during the
    // fade are played again (unless the arp owns the keys), and it starts
    // without a fade-in so the transient stays sharp.
    const bool oneShot = audioParams.geti (P::EngineMode) == (int) EngineMode::Kick && ! audioParams.on (P::ArpOn);
    if (restart)
    {
        size_t count = 0;
        const auto add = [&] (MidiEvent::Type type, int number, float value) {
            auto& e = restartEvents[count++];
            e = {};
            e.type = type;
            e.number = number;
            e.value = value;
        };
        if (replaySustain && ! oneShot)
            add (MidiEvent::Controller, 64, 1.f);
        for (int note = 0; note < 128; ++note)
        {
            const float v = oneShot ? replay[(size_t) note] : std::max (replay[(size_t) note], replayPedal[(size_t) note]);
            if (v > 0.f && (! oneShot || pressedInFade[(size_t) note]))
                add (MidiEvent::NoteOn, note, v);
        }
        if (! oneShot)
            for (int note = 0; note < 128; ++note)
                if (replayPedal[(size_t) note] > 0.f && replay[(size_t) note] <= 0.f)
                    add (MidiEvent::NoteOff, note, 0.f);
        count = std::min (count, midiEvents.capacity() - midiEvents.size());
        midiEvents.insert (midiEvents.begin(), restartEvents.begin(), restartEvents.begin() + (std::ptrdiff_t) count);
        pressedInFade.fill (false);
        if (oneShot) swap = Swap::None;
    }

    buffer.clear();
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;
    engine.setOffline (isNonRealtime());   // a host render plays at the Render quality
    engine.process (l, r, n, midiEvents.data(), (int) midiEvents.size(), transport);

    if (swap == Swap::FadingOut)
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = std::max (0.f, 1.f - (float) fadePos / (float) fadeOutLen);
            l[i] *= g;
            if (r != l) r[i] *= g;
            ++fadePos;
        }
        // silent until the new patch is all written (the epoch first: a change
        // that opens after this read shows up as a new epoch next block)
        const uint32_t epoch = patchEpoch.load();
        if (fadePos >= fadeOutLen && (openChanges.load() == 0 || fadePos >= fadeOutLen + maxHold))
        {
            engine.reset();   // voices, filter states, delay and reverb tails; params jump, no smoothing
            seenEpoch = epoch;
            swap = Swap::Restart;
            fadePos = 0;
        }
    }
    else if (swap == Swap::Restart)
    {
        const int m = std::min (n, fadeInLen - fadePos);
        for (int i = 0; i < m; ++i)
        {
            const float g = (float) (fadePos + i) / (float) fadeInLen;
            l[i] *= g;
            if (r != l) r[i] *= g;
        }
        fadePos += m;
        if (fadePos >= fadeInLen) swap = Swap::None;
    }

    const int lat = engine.latencySamples();
    if (lat != reportedLatency)
    {
        reportedLatency = lat;
        setLatencySamples (lat);
    }

    // scope + meters
    int w = scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        scope[(size_t) w] = 0.5f * (l[i] + r[i]);
        w = (w + 1) & (kScopeSize - 1);
    }
    scopeWrite.store (w, std::memory_order_release);
    const float rmsCoef = 1.f - std::exp (-(float) n / (0.3f * (float) getSampleRate()));
    for (size_t c = 0; c < 2; ++c)
    {
        const float* x = c == 0 ? l : r;
        float pk = 0.f, sq = 0.f;
        for (int i = 0; i < n; ++i)
        {
            pk = std::max (pk, std::abs (x[i]));
            sq += x[i] * x[i];
        }
        meanSquare[c] += rmsCoef * (sq / (float) std::max (n, 1) - meanSquare[c]);
        outMeanSquare[c].store (meanSquare[c], std::memory_order_relaxed);
        if (pk > outPeak[c].load (std::memory_order_relaxed))   // only the GUI lowers it (takeOutLevel)
            outPeak[c].store (pk, std::memory_order_relaxed);
    }
    arpStep.store (engine.arpStep(), std::memory_order_relaxed);
    if (const juce::SpinLock::ScopedTryLockType tl (modViewLock); tl.isLocked())
        engine.getModView (modView);

    const auto& cm = engine.compMeter();
    const float fall = 45.f * (float) n / (float) getSampleRate();
    for (size_t b = 0; b < 3; ++b)
    {
        auto& m = compMeters[b];
        m[0].store (std::max (cm.inDb[b], m[0].load (std::memory_order_relaxed) - fall), std::memory_order_relaxed);
        m[1].store (cm.gainDb[b], std::memory_order_relaxed);
        m[2].store (cm.totalDb[b], std::memory_order_relaxed);
    }
}

void PsyBoxProcessor::copyScope (float* dest, int num) const noexcept
{
    const int w = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < num; ++i)
        dest[i] = scope[(size_t) ((w - num + i) & (kScopeSize - 1))];
}

// ---------------------------------------------------------------------------
// Generator
// ---------------------------------------------------------------------------

void PsyBoxProcessor::applyState (const ParamState& state)
{
    // Whatever the source (recipe, init, preset, undo), a kick category plays
    // the kick engine, Phoscyon the 303 and every other category the synth,
    // and only synth categories keep an arp (old basses and kicks may have
    // one switched on).
    auto s = state;
    s.set (P::EngineMode, (float) categoryEngine());
    // the volume and the qualities are global settings, not part of a sound
    // (init, undo, presets and category slots keep them too)
    s.set (P::MasterVol, plain (P::MasterVol));
    s.set (P::Quality, plain (P::Quality));
    s.set (P::RenderQuality, plain (P::RenderQuality));
    if (! hasArp (category))
        s.set (P::ArpOn, 0.f);
    const auto& defs = paramDefs();
    for (size_t i = 0; i < (size_t) kNumParams; ++i)
    {
        const float target = s.v[i];
        if (std::abs (rawValues[i]->load() - target) < 1e-7f)
            continue;
        auto* p = params[i];
        p->beginChangeGesture();
        p->setValueNotifyingHost (defs[i].toNorm (target));
        p->endChangeGesture();
    }
}

void PsyBoxProcessor::setCategory (Category c)
{
    if (c == category) return;
    const PatchChange change (*this);   // the switch and, on a first visit, the new sound: one swap
    switchCategory (c);
    if (slots[(size_t) c].valid)
    {
        pushHistory();
        sendChangeMessage();
    }
    else
    {
        generate();   // first visit: a fresh sound of this kind
    }
}

void PsyBoxProcessor::saveSlot()
{
    const juce::ScopedLock sl (stateLock);
    slots[(size_t) category] = { true, currentState(), seed, presetName, macroNames };
}

void PsyBoxProcessor::switchCategory (Category c)
{
    const PatchChange change (*this);
    saveSlot();
    {
        const juce::ScopedLock sl (stateLock);
        category = c;
        presetFile = juce::File();
        if (! slots[(size_t) c].valid) return;
        const auto& slot = slots[(size_t) c];
        seed = slot.seed;
        presetName = slot.name;
        macroNames = slot.macros;
    }
    applyState (slots[(size_t) c].state);
    restoreRanges();
}

void PsyBoxProcessor::writeSlots (juce::ValueTree& t) const
{
    juce::ValueTree all ("Slots");
    const auto& defs = paramDefs();
    for (int c = 0; c < kNumCategories; ++c)
    {
        const auto& slot = slots[(size_t) c];
        if (! slot.valid || c == (int) category) continue;   // the current category is the live patch
        juce::ValueTree st ("Slot");
        st.setProperty ("category", c, nullptr);
        st.setProperty ("seed", (int) slot.seed, nullptr);
        st.setProperty ("name", slot.name, nullptr);
        // not "macro1": that is Macro 1's parameter id, written below
        for (int i = 0; i < kNumMacros; ++i)
            st.setProperty ("macroName" + juce::String (i + 1), slot.macros[(size_t) i], nullptr);
        for (size_t i = 0; i < (size_t) kNumParams; ++i)
            st.setProperty (juce::Identifier (defs[i].id), slot.state.v[i], nullptr);
        all.appendChild (st, nullptr);
    }
    t.removeChild (t.getChildWithName ("Slots"), nullptr);
    t.appendChild (all, nullptr);
}

void PsyBoxProcessor::readSlots (const juce::ValueTree& t)
{
    for (auto& slot : slots) slot = {};
    const auto& defs = paramDefs();
    for (const auto& st : t.getChildWithName ("Slots"))
    {
        const int c = (int) st.getProperty ("category", -1);
        if (c < 0 || c >= kNumCategories) continue;
        Slot slot { true, ParamState(), (uint32_t) (int) st.getProperty ("seed", 0), st.getProperty ("name").toString(), {} };
        // Sessions before 0.5.3 kept slot names under "macro1".., which the
        // macro knobs' values overwrote: those names were lost, so automatic.
        for (int i = 0; i < kNumMacros; ++i)
            slot.macros[(size_t) i] = parseMacroName (st.getProperty ("macroName" + juce::String (i + 1), {}), i);
        for (size_t i = 0; i < (size_t) kNumParams; ++i)
            if (const auto* v = st.getPropertyPointer (juce::Identifier (defs[i].id)))
                slot.state.v[i] = defs[i].snap ((float) *v);
        slots[(size_t) c] = slot;
    }
}

void PsyBoxProcessor::generate()
{
    generateWithSeed ((uint32_t) random.nextInt() & 0xFFFFFFu);
}

void PsyBoxProcessor::generateWithSeed (uint32_t s)
{
    // the new sound first (tens of ms) while the old one plays on, then one short swap
    auto patch = generator.generate (category, s, currentState(), locks, arpGeneration, kickKeyGeneration);
    levelPatch (patch, category, locks);   // every new patch about as loud as the last
    const PatchChange change (*this);
    applyState (patch);
    {
        const juce::ScopedLock sl (stateLock);
        seed = s;
        resetMacroNames();   // a new sound: the macros are named after what they now drive
        presetName = juce::String (categoryInfo()[(size_t) category].name) + " #" + juce::String::toHexString ((int) seed).toUpperCase().paddedLeft ('0', 6);
        presetFile = juce::File();
    }
    pushHistory();
    sendChangeMessage();
}

void PsyBoxProcessor::setArpGeneration (bool on)
{
    arpGeneration = on;
    sendChangeMessage();
}

void PsyBoxProcessor::setKickKeyGeneration (bool on)
{
    kickKeyGeneration = on;
    sendChangeMessage();
}

void PsyBoxProcessor::setArpPattern (const ArpPattern& p)
{
    auto s = currentState();
    p.writeTo (s);
    applyState (s);
}

// No patch-swap fade here: the sound stays, only the steps change.
void PsyBoxProcessor::generateArpPattern()
{
    auto patch = generator.generateArpPattern (category, (uint32_t) random.nextInt(), currentState(), locks);
    levelPatch (patch, category, locks);
    applyState (patch);
    markEdited();
    pushHistory();
    sendChangeMessage();
}

void PsyBoxProcessor::mutate()
{
    auto patch = generator.mutate (mutateAmount, (uint32_t) random.nextInt(), currentState(), locks);
    levelPatch (patch, category, locks);
    const PatchChange change (*this);
    applyState (patch);
    markEdited();
    pushHistory();
    sendChangeMessage();
}

void PsyBoxProcessor::markEdited()
{
    const juce::ScopedLock sl (stateLock);
    if (! presetName.endsWith ("*"))
        presetName << "*";
}

void PsyBoxProcessor::setLocked (P p, bool shouldLock)
{
    {
        const juce::ScopedLock sl (stateLock);
        locks.set ((size_t) p, shouldLock);
    }
    sendChangeMessage();
}

void PsyBoxProcessor::clearLocks()
{
    {
        const juce::ScopedLock sl (stateLock);
        locks.reset();
    }
    sendChangeMessage();
}

void PsyBoxProcessor::pushHistory()
{
    if (historyIndex + 1 < (int) history.size())
        history.erase (history.begin() + historyIndex + 1, history.end());
    history.push_back ({ currentState(), seed, category, presetName, macroNames });
    if (history.size() > 64)
        history.erase (history.begin());
    historyIndex = (int) history.size() - 1;
}

void PsyBoxProcessor::undo()
{
    if (canUndo()) stepHistory (-1);
}

void PsyBoxProcessor::redo()
{
    if (canRedo()) stepHistory (1);
}

void PsyBoxProcessor::stepHistory (int delta)
{
    const PatchChange change (*this);
    // keep manual tweaks made since the last generator action reachable
    // from the other direction
    history[(size_t) historyIndex].state = currentState();
    history[(size_t) historyIndex].macros = macroNames;
    historyIndex += delta;
    const auto& h = history[(size_t) historyIndex];
    if (h.category != category) saveSlot();
    {
        const juce::ScopedLock sl (stateLock);
        seed = h.seed;
        category = h.category;
        presetName = h.name;
        macroNames = h.macros;
    }
    applyState (h.state);
    restoreRanges();
    sendChangeMessage();
}

// Mutate needs the recipe's ranges; they are a pure function of category and
// seed (plus whether the patch has an arp), so re-run the recipe on a scratch
// patch instead of storing them.
void PsyBoxProcessor::restoreRanges()
{
    Generator scratch;
    scratch.generate (category, seed, ParamState(), LockSet(), plain (P::ArpOn) > 0.5f, plain (P::KickKeyTrack) > 0.5f);
    generator.setRanges (scratch.lastRanges());
}

juce::String PsyBoxProcessor::getMacroName (int i) const
{
    if (macroNames[(size_t) i].isNotEmpty())
        return macroNames[(size_t) i];
    const auto label = macroLabel (currentState(), i);
    return label.empty() ? "Macro " + juce::String (i + 1) : juce::String (label);
}

bool PsyBoxProcessor::macroDrivesAnything (int i) const
{
    return ! macroLabel (currentState(), i).empty();
}

void PsyBoxProcessor::setMacroName (int i, const juce::String& name)
{
    // Typing the automatic name, a default name or nothing goes back to automatic.
    auto n = name.trim().substring (0, 14);
    const auto automatic = macroLabel (currentState(), i);
    if (n == juce::String (automatic) || n == "Macro " + juce::String (i + 1))
        n = {};
    {
        const juce::ScopedLock sl (stateLock);
        macroNames[(size_t) i] = n;
    }
    sendChangeMessage();
}

void PsyBoxProcessor::resetMacroNames()
{
    const juce::ScopedLock sl (stateLock);
    for (auto& n : macroNames) n = {};
}

juce::String PsyBoxProcessor::parseMacroName (const juce::var& stored, int i)
{
    const auto n = stored.toString().trim();
    return n == "Macro " + juce::String (i + 1) ? juce::String() : n;
}

// On the state's root: the parameters are children there, so "macro1" is free.
void PsyBoxProcessor::writeMacroNames (juce::ValueTree& t) const
{
    for (int i = 0; i < kNumMacros; ++i)
        t.setProperty ("macro" + juce::String (i + 1), macroNames[(size_t) i], nullptr);
}

void PsyBoxProcessor::readMacroNames (const juce::ValueTree& t)
{
    const juce::ScopedLock sl (stateLock);
    for (int i = 0; i < kNumMacros; ++i)
        macroNames[(size_t) i] = parseMacroName (t.getProperty ("macro" + juce::String (i + 1), {}), i);
}

// ---------------------------------------------------------------------------
// Modulation
// ---------------------------------------------------------------------------

std::vector<PsyBoxProcessor::ModRoute> PsyBoxProcessor::getModRoutes() const
{
    std::vector<ModRoute> out;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const int src = (int) std::lround (plain (modSrcParam (s)));
        const int dst = (int) std::lround (plain (modDstParam (s)));
        if (src > 0 && dst > 0 && src < (int) ModSrc::Count && dst < (int) ModDst::Count)
            out.push_back ({ s, (ModSrc) src, (ModDst) dst, plain (modAmtParam (s)) });
    }
    return out;
}

int PsyBoxProcessor::modRoutesTo (ModDst dst, std::array<ModRoute, kNumModSlots>& out) const
{
    int n = 0;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const int src = (int) std::lround (plain (modSrcParam (s)));
        if (src > 0 && src < (int) ModSrc::Count && (int) std::lround (plain (modDstParam (s))) == (int) dst)
            out[(size_t) n++] = { s, (ModSrc) src, dst, plain (modAmtParam (s)) };
    }
    return n;
}

int PsyBoxProcessor::findModSlot (ModSrc src, ModDst dst) const
{
    for (const auto& r : getModRoutes())
        if (r.src == src && r.dst == dst) return r.slot;
    return -1;
}

namespace
{
    void setPlainAsGesture (juce::RangedAudioParameter* p, const ParamDef& d, float plain)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (d.toNorm (plain));
        p->endChangeGesture();
    }
} // namespace

int PsyBoxProcessor::addModulation (ModSrc src, ModDst dst, float amount)
{
    if (src == ModSrc::None || dst == ModDst::None) return -1;
    if (const int existing = findModSlot (src, dst); existing >= 0)
        return existing;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const bool free = std::lround (plain (modSrcParam (s))) == 0 || std::lround (plain (modDstParam (s))) == 0;
        if (! free) continue;
        setPlainAsGesture (param (modSrcParam (s)), def (modSrcParam (s)), (float) src);
        setPlainAsGesture (param (modDstParam (s)), def (modDstParam (s)), (float) dst);
        setPlainAsGesture (param (modAmtParam (s)), def (modAmtParam (s)), amount);
        sendChangeMessage();   // macro names follow
        return s;
    }
    return -1;
}

void PsyBoxProcessor::removeModulation (int slot)
{
    if (slot < 0 || slot >= kNumModSlots) return;
    setPlainAsGesture (param (modSrcParam (slot)), def (modSrcParam (slot)), 0.f);
    setPlainAsGesture (param (modDstParam (slot)), def (modDstParam (slot)), 0.f);
    setPlainAsGesture (param (modAmtParam (slot)), def (modAmtParam (slot)), 0.f);
    sendChangeMessage();
}

void PsyBoxProcessor::beginModAmountChange (int slot)
{
    if (slot >= 0 && slot < kNumModSlots) param (modAmtParam (slot))->beginChangeGesture();
}

void PsyBoxProcessor::setModAmount (int slot, float amount)
{
    if (slot < 0 || slot >= kNumModSlots) return;
    const auto& d = def (modAmtParam (slot));
    param (modAmtParam (slot))->setValueNotifyingHost (d.toNorm (juce::jlimit (-1.f, 1.f, amount)));
}

void PsyBoxProcessor::endModAmountChange (int slot)
{
    if (slot >= 0 && slot < kNumModSlots) param (modAmtParam (slot))->endChangeGesture();
}

// ---------------------------------------------------------------------------
// Energy length
// ---------------------------------------------------------------------------

double PsyBoxProcessor::energyLengthSeconds() const
{
    if ((int) std::lround (plain (P::EnergyMode)) == (int) EnergyMode::Time)
        return plain (P::EnergySec);
    return energyBarsValue ((int) std::lround (plain (P::EnergyBars))) * 240.0 / juce::jmax (20.0, getBpm());
}

void PsyBoxProcessor::setEnergyMode (EnergyMode m)
{
    const double seconds = energyLengthSeconds();
    if (m == EnergyMode::Time)
    {
        setPlainAsGesture (param (P::EnergySec), def (P::EnergySec), juce::jlimit (0.1f, 60.f, (float) seconds));
    }
    else
    {
        // the bar length nearest to what it sounds like now
        const double bars = seconds * juce::jmax (20.0, getBpm()) / 240.0;
        int best = 0;
        for (int i = 1; i < def (P::EnergyBars).numChoices(); ++i)
            if (std::abs (std::log (energyBarsValue (i) / bars)) < std::abs (std::log (energyBarsValue (best) / bars)))
                best = i;
        setPlainAsGesture (param (P::EnergyBars), def (P::EnergyBars), (float) best);
    }
    setPlainAsGesture (param (P::EnergyMode), def (P::EnergyMode), (float) m);
}

void PsyBoxProcessor::initPatch()
{
    const PatchChange change (*this);
    applyState (ParamState());
    {
        const juce::ScopedLock sl (stateLock);
        resetMacroNames();
        presetName = "Init";
        presetFile = juce::File();
    }
    pushHistory();
    sendChangeMessage();
}

double PsyBoxProcessor::getTailLengthSeconds() const
{
    // What sounds on after the last note-off: the release, then the echoes
    // (to -60 dB) and the reverb. Capped, so an infinite feedback stays finite.
    const auto mode = (EngineMode) std::lround (plain (P::EngineMode));
    const double bpm = juce::jmax (20.0, getBpm());
    const auto echoes = [] (double seconds, double feedback) {
        return feedback <= 1.0e-3 ? seconds : seconds * std::log (1.0e-3) / std::log (juce::jmin (feedback, 0.999));
    };
    double tail = 0.0;
    if (mode == EngineMode::Phoscyon)
    {
        tail = 0.5;   // the 303's VCA lets go within a few hundred ms
        if (plain (P::PhosDlyOn) > 0.5f)
        {
            const double t = plain (P::PhosDlySync) > 0.5f
                                ? PhosFx::delayMs ((int) std::lround (plain (P::PhosDlyLNum)), (int) std::lround (plain (P::PhosDlyLDen)),
                                                   (int) std::lround (plain (P::PhosDlyLScale)), bpm) / 1000.0
                                : (double) plain (P::PhosDlyTime);
            tail += echoes (t, plain (P::PhosDlyFb));
        }
        if (plain (P::PhosRevOn) > 0.5f)
            tail += 10.0;
    }
    else
    {
        tail = (mode == EngineMode::Kick ? plain (P::KickDecay) : plain (P::AenvR)) / 1000.0;
        if (plain (P::DelayOn) > 0.5f)
        {
            const double t = plain (P::DelaySync) > 0.5f ? syncDivBeats ((int) std::lround (plain (P::DelayDiv))) * 60.0 / bpm
                                                         : plain (P::DelayMs) / 1000.0;
            tail += echoes (t, plain (P::DelayFb));
        }
        if (plain (P::ReverbOn) > 0.5f)
            tail += plain (P::ReverbDecay);
    }
    return juce::jlimit (0.1, 60.0, tail);
}

// ---------------------------------------------------------------------------
// Bounce
// ---------------------------------------------------------------------------

PsyBoxProcessor::Bounce PsyBoxProcessor::bounceRequest() const
{
    Bounce b;
    b.state = currentState();
    if (b.state.geti (P::EngineMode) == (int) EngineMode::Kick)
        b.state.set (P::ArpOn, 0.f);   // one hit: the WAV is a kick sample
    b.bpm = juce::jmax (20.0, lastBpm.load());
    b.note = lastPlayedNote.load();

    // Gate: percussive patches (no sustain) only need attack + decay; sustained
    // ones are held for at least a beat so the body of the sound is there.
    // Energy sounds are held for their whole length.
    const double attack = b.state.get (P::AenvA) / 1000.0, decay = b.state.get (P::AenvD) / 1000.0;
    const double beat = 60.0 / b.bpm;
    b.gateSec = hasLength() ? juce::jlimit (0.1, 60.0, energyLengthSeconds())
              : b.state.get (P::AenvS) < 0.05f ? juce::jlimit (0.1, 8.0, attack + decay)
                                               : juce::jlimit (beat, 8.0, attack + decay + 0.25);
    b.expectSec = b.gateSec + juce::jmin (getTailLengthSeconds(), 32.0);

    // Flats: a file name can't hold "#", and C#3 would become C3.
    const auto noteName = juce::MidiMessage::getMidiNoteName (b.note, false, true, 4);
    b.name = juce::File::createLegalFileName (presetName.trimCharactersAtEnd ("*") + " " + noteName);

    // FNV-1a over everything the render depends on
    uint64_t h = 1469598103934665603ULL;
    const auto mix = [&h] (uint64_t v) { h = (h ^ v) * 1099511628211ULL; };
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = b.state.get ((P) i);
        uint32_t bits;
        std::memcpy (&bits, &v, sizeof bits);
        mix (bits);
    }
    mix ((uint64_t) std::llround (b.bpm * 1000.0));
    mix ((uint64_t) b.note);
    mix ((uint64_t) b.name.hashCode64());
    b.key = h;
    return b;
}

bool PsyBoxProcessor::renderBounce (const Bounce& b, const juce::File& file, const std::function<bool()>& cancelled,
                                    std::atomic<float>* progress)
{
    constexpr double fs = 48000.0;
    constexpr int block = 512;
    const int gate = (int) (b.gateSec * fs);
    const int maxLen = (int) ((b.gateSec + 32.0) * fs);
    const double expected = std::max (1.0, b.expectSec * fs);

    Engine e;
    e.setOffline (true);    // a bounce plays at the Render quality
    e.setParams (b.state);  // before prepare, so the first note and the latency see this patch
    e.prepare (fs, block);
    const int latency = e.latencySamples();

    std::vector<float> L, R;
    L.reserve ((size_t) (gate + 10 * fs));
    R.reserve (L.capacity());
    std::array<float, block> bl {}, br {};
    int pos = 0, quiet = 0;
    const int quietNeeded = (int) (0.3 * fs);
    while (pos < maxLen)
    {
        if (cancelled())
            return false;
        MidiEvent ev[2];
        int nEv = 0;
        if (pos == 0) { ev[nEv].type = MidiEvent::NoteOn; ev[nEv].number = b.note; ev[nEv].value = 0.8f; ++nEv; }
        if (gate >= pos && gate < pos + block) { ev[nEv].type = MidiEvent::NoteOff; ev[nEv].number = b.note; ev[nEv].offset = gate - pos; ++nEv; }
        e.process (bl.data(), br.data(), block, ev, nEv, b.bpm);
        float pk = 0.f;
        for (int i = 0; i < block; ++i)
        {
            L.push_back (bl[(size_t) i]);
            R.push_back (br[(size_t) i]);
            pk = std::max (pk, std::max (std::abs (bl[(size_t) i]), std::abs (br[(size_t) i])));
        }
        pos += block;
        if (progress != nullptr)
            progress->store ((float) std::min (0.99, pos / expected), std::memory_order_relaxed);
        // after the note is released, stop once the tail stays below -80 dB
        quiet = (pos > gate && pk < 1e-4f) ? quiet + block : 0;
        if (quiet >= quietNeeded) break;
    }

    // drop the decimator latency at the start and the silence at the end
    int end = (int) L.size();
    while (end > latency + 1 && std::max (std::abs (L[(size_t) end - 1]), std::abs (R[(size_t) end - 1])) < 1e-4f) --end;
    const int len = std::max (1, end - latency);
    juce::AudioBuffer<float> out (2, len);
    out.copyFrom (0, 0, L.data() + latency, len);
    out.copyFrom (1, 0, R.data() + latency, len);
    const int fade = std::min (len, (int) (0.005 * fs));
    out.applyGainRamp (len - fade, fade, 1.f, 0.f);
    // normalise to -1 dBFS peak so the sample drops in at a usable level
    if (const float pk = out.getMagnitude (0, len); pk > 1e-5f)
        out.applyGain (juce::Decibels::decibelsToGain (-1.f) / pk);

    juce::WavAudioFormat wav;
    if (! file.deleteFile())   // an output stream would append to it
        return false;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (out, 0, len))
    {
        writer.reset();
        stream.reset();
        file.deleteFile();
        return false;
    }
    writer.reset();
    if (progress != nullptr)
        progress->store (1.f, std::memory_order_relaxed);
    return true;
}

juce::File PsyBoxProcessor::newBounceFile (const Bounce& b)
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("PsyBox").getChildFile ("Bounces");
    if (! dir.createDirectory())
        return {};
    return dir.getChildFile (b.name + ".wav").getNonexistentSibling();
}

// ---------------------------------------------------------------------------
// State & presets
// ---------------------------------------------------------------------------

void PsyBoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    const juce::ScopedLock sl (stateLock);
    state.setProperty ("stateVersion", kStateVersion, nullptr);
    state.setProperty ("category", (int) category, nullptr);
    state.setProperty ("seed", (int) seed, nullptr);
    state.setProperty ("mutate", mutateAmount, nullptr);
    state.setProperty ("arpGen", arpGeneration, nullptr);
    state.setProperty ("kickKeyGen", kickKeyGeneration, nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    writeMacroNames (state);
    writeSlots (state);
    juce::StringArray lockIds;
    for (int i = 0; i < kNumParams; ++i)
        if (locks.test ((size_t) i))
            lockIds.add (paramDefs()[(size_t) i].id);
    state.setProperty ("locks", lockIds.joinIntoString (","), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PsyBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (! adoptStateTag (xml.get(), apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    // Sessions saved before a parameter existed (the arp) get its default.
    for (const auto& d : paramDefs())
        if (! tree.getChildWithProperty ("id", juce::String (d.id)).isValid())
        {
            juce::ValueTree child ("PARAM");
            child.setProperty ("id", juce::String (d.id), nullptr);
            child.setProperty ("value", d.def, nullptr);
            tree.appendChild (child, nullptr);
        }
    // A host preset or A/B compare can load while notes play: one clean swap.
    const PatchChange change (*this);
    apvts.replaceState (tree);
    {
        const juce::ScopedLock sl (stateLock);
        category = (Category) juce::jlimit (0, kNumCategories - 1, (int) tree.getProperty ("category", 0));
        seed = (uint32_t) (int) tree.getProperty ("seed", 0);
        setMutateAmount ((float) tree.getProperty ("mutate", 0.25f));
        arpGeneration = (bool) tree.getProperty ("arpGen", false);
        kickKeyGeneration = (bool) tree.getProperty ("kickKeyGen", true);
        presetName = tree.getProperty ("presetName", "Init").toString();
        presetFile = juce::File();
        readMacroNames (tree);
        readSlots (tree);
        locks.reset();
        for (const auto& id : juce::StringArray::fromTokens (tree.getProperty ("locks").toString(), ",", ""))
            if (const int idx = findParam (id.toStdString()); idx >= 0)
                locks.set ((size_t) idx);
    }
    normaliseFamily();
    apvts.state.removeChild (apvts.state.getChildWithName ("Slots"), nullptr);
    restoreRanges();
    history.clear();
    historyIndex = -1;
    pushHistory();
    sendChangeMessage();
}

void PsyBoxProcessor::normaliseFamily()
{
    const float want = (float) categoryEngine();
    if (std::abs (plain (P::EngineMode) - want) > 0.5f)
        params[(size_t) P::EngineMode]->setValueNotifyingHost (def (P::EngineMode).toNorm (want));
    if (! hasArp (category) && plain (P::ArpOn) > 0.5f)
        params[(size_t) P::ArpOn]->setValueNotifyingHost (0.f);
}

juce::File PsyBoxProcessor::presetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("PsyBox").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> PsyBoxProcessor::listPresets() const
{
    auto files = presetFolder().findChildFiles (juce::File::findFiles, true, "*.psyp");
    files.sort();
    return files;
}

juce::File PsyBoxProcessor::presetFileFor (const juce::String& name) const
{
    return presetFolder().getChildFile (categoryInfo()[(size_t) category].name)
                         .getChildFile (juce::File::createLegalFileName (name.trim()) + ".psyp");
}

bool PsyBoxProcessor::savePreset (const juce::String& name)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", kStateVersion, nullptr);
    state.setProperty ("category", (int) category, nullptr);
    state.setProperty ("seed", (int) seed, nullptr);
    writeMacroNames (state);
    auto xml = state.createXml();
    if (xml == nullptr) return false;
    const auto file = presetFileFor (name);
    if (file.getFileNameWithoutExtension().isEmpty() || ! file.getParentDirectory().createDirectory())
        return false;
    if (! xml->writeTo (file))
        return false;
    {
        const juce::ScopedLock sl (stateLock);
        presetName = file.getFileNameWithoutExtension();   // what the preset list shows
        presetFile = file;
    }
    sendChangeMessage();
    return true;
}

bool PsyBoxProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (! adoptStateTag (xml.get(), apvts.state.getType()))
        return false;
    const auto tree = juce::ValueTree::fromXml (*xml);
    const PatchChange change (*this);
    ParamState s;
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild (i);
        const int idx = findParam (child.getProperty ("id").toString().toStdString());
        if (idx >= 0)
            s.v[(size_t) idx] = paramDefs()[(size_t) idx].snap ((float) child.getProperty ("value"));
    }
    saveSlot();
    {
        const juce::ScopedLock sl (stateLock);
        category = (Category) juce::jlimit (0, kNumCategories - 1, (int) tree.getProperty ("category", (int) category));
        seed = (uint32_t) (int) tree.getProperty ("seed", (int) seed);
        readMacroNames (tree);
        presetName = file.getFileNameWithoutExtension();
        presetFile = file;
    }
    applyState (s);
    restoreRanges();
    pushHistory();
    sendChangeMessage();
    return true;
}

void PsyBoxProcessor::stepPreset (int delta)
{
    const auto files = listPresets();
    if (files.isEmpty()) return;
    // From the preset the patch came from (edited or not); a name alone can
    // be in two category folders.
    int idx = files.indexOf (presetFile);
    if (idx < 0)
        for (int i = 0; i < files.size(); ++i)
            if (files[i].getFileNameWithoutExtension() == presetName.trimCharactersAtEnd ("*")) { idx = i; break; }
    idx = idx < 0 ? (delta > 0 ? 0 : files.size() - 1) : (idx + delta + files.size()) % files.size();
    loadPreset (files[idx]);
}

juce::AudioProcessorEditor* PsyBoxProcessor::createEditor()
{
    return new PsyBoxEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsyBoxProcessor();
}
