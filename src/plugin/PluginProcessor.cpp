#include "PluginProcessor.h"

#include "PluginEditor.h"

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
            const ParamDef* dp = &d;
            auto attrs = juce::AudioParameterFloatAttributes()
                             .withStringFromValueFunction ([dp] (float v, int) { return juce::String (dp->format (v)); })
                             .withValueFromStringFunction ([dp] (const juce::String& s) { return dp->snap (s.getFloatValue()); });
            layout.add (std::make_unique<juce::AudioParameterFloat> (id, name, rangeFor (d), d.def, attrs));
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
    engine.setParams (currentState());
    engine.prepare (sampleRate, samplesPerBlock);
    reportedLatency = engine.latencySamples();
    setLatencySamples (reportedLatency);
    swap = Swap::None;
    seenEpoch = patchEpoch.load();
    heldVelocity.fill (0.f);
}

ParamState PsyBoxProcessor::currentState() const
{
    ParamState s;
    for (size_t i = 0; i < (size_t) kNumParams; ++i)
        s.v[i] = rawValues[i]->load (std::memory_order_relaxed);
    return s;
}

void PsyBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    if (swap == Swap::None && patchEpoch.load (std::memory_order_acquire) != seenEpoch)
    {
        swap = Swap::FadingOut;   // keep the old values for the fade
        fadePos = 0;
    }
    if (swap != Swap::FadingOut)
    {
        for (size_t i = 0; i < (size_t) kNumParams; ++i)
            audioParams.v[i] = rawValues[i]->load (std::memory_order_relaxed);
        engine.setParams (audioParams);
    }

    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    midiEvents.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        MidiEvent e;
        e.offset = juce::jlimit (0, n - 1, meta.samplePosition);
        if (m.isNoteOn())
        {
            e.type = MidiEvent::NoteOn; e.number = m.getNoteNumber(); e.value = m.getFloatVelocity();
            lastPlayedNote.store (e.number, std::memory_order_relaxed);
            heldVelocity[(size_t) e.number] = std::max (0.01f, e.value);
            if (swap == Swap::FadingOut) pressedInFade[(size_t) e.number] = true;
        }
        else if (m.isNoteOff()) { e.type = MidiEvent::NoteOff; e.number = m.getNoteNumber(); heldVelocity[(size_t) e.number] = 0.f; }
        else if (m.isPitchWheel()) { e.type = MidiEvent::PitchBend; e.value = (float) (m.getPitchWheelValue() - 8192) / 8192.f; }
        else if (m.isController()) { e.type = MidiEvent::Controller; e.number = m.getControllerNumber(); e.value = (float) m.getControllerValue() / 127.f; }
        else if (m.isChannelPressure()) { e.type = MidiEvent::ChannelPressure; e.value = (float) m.getChannelPressureValue() / 127.f; }
        else if (m.isAftertouch()) { e.type = MidiEvent::ChannelPressure; e.value = (float) m.getAfterTouchValue() / 127.f; }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { e.type = MidiEvent::AllNotesOff; heldVelocity.fill (0.f); }
        else continue;
        if (midiEvents.size() < midiEvents.capacity())
            midiEvents.push_back (e);
    }

    Transport transport;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            if (const auto b = pos->getBpm())
                transport.bpm = *b;
            if (const auto ppq = pos->getPpqPosition())
            {
                transport.ppq = *ppq;
                transport.playing = pos->getIsPlaying();
            }
        }
    const double bpm = transport.bpm;
    lastBpm.store (bpm, std::memory_order_relaxed);

    // First block after a reset: the new patch starts clean, held notes come back.
    // A kick is a one-shot: only keys pressed during the fade are played again
    // (unless the arp owns the keys), and it starts without a fade-in so the
    // transient stays sharp.
    const bool oneShot = audioParams.geti (P::EngineMode) == (int) EngineMode::Kick && ! audioParams.on (P::ArpOn);
    if (swap == Swap::Restart && fadePos == 0)
    {
        for (int note = 0; note < 128; ++note)
            if (heldVelocity[(size_t) note] > 0.f && (! oneShot || pressedInFade[(size_t) note])
                && midiEvents.size() < midiEvents.capacity())
            {
                MidiEvent e;
                e.type = MidiEvent::NoteOn;
                e.number = note;
                e.value = heldVelocity[(size_t) note];
                midiEvents.insert (midiEvents.begin(), e);
            }
        pressedInFade.fill (false);
        if (oneShot) swap = Swap::None;
    }

    buffer.clear();
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;
    engine.process (l, r, n, midiEvents.data(), (int) midiEvents.size(), transport);

    if (swap == Swap::FadingOut)
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = std::max (0.f, 1.f - (float) fadePos / (float) kFadeOut);
            l[i] *= g;
            if (r != l) r[i] *= g;
            ++fadePos;
        }
        if (fadePos >= kFadeOut)
        {
            engine.reset();   // voices, filter states, delay and reverb tails; params jump, no smoothing
            seenEpoch = patchEpoch.load (std::memory_order_acquire);
            swap = Swap::Restart;
            fadePos = 0;
        }
    }
    else if (swap == Swap::Restart)
    {
        const int m = std::min (n, kFadeIn - fadePos);
        for (int i = 0; i < m; ++i)
        {
            const float g = (float) (fadePos + i) / (float) kFadeIn;
            l[i] *= g;
            if (r != l) r[i] *= g;
        }
        fadePos += m;
        if (fadePos >= kFadeIn) swap = Swap::None;
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
    // the kick engine and every other category the synth, and only synth
    // categories keep an arp (old basses and kicks may have one switched on).
    auto s = state;
    s.set (P::EngineMode, (float) (isKickCategory() ? EngineMode::Kick : EngineMode::Synth));
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
    slots[(size_t) category] = { true, currentState(), seed, presetName, macroNames };
}

void PsyBoxProcessor::switchCategory (Category c)
{
    beginPatchChange();
    saveSlot();
    category = c;
    const auto& slot = slots[(size_t) c];
    if (! slot.valid) return;
    auto s = slot.state;
    s.set (P::Quality, plain (P::Quality));     // global settings are not part of a sound
    s.set (P::MasterVol, plain (P::MasterVol));
    seed = slot.seed;
    presetName = slot.name;
    macroNames = slot.macros;
    applyState (s);
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
        for (int i = 0; i < kNumMacros; ++i)
            st.setProperty ("macro" + juce::String (i + 1), slot.macros[(size_t) i], nullptr);
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
        for (int i = 0; i < kNumMacros; ++i)
        {
            const auto n = st.getProperty ("macro" + juce::String (i + 1), {}).toString();
            slot.macros[(size_t) i] = n == "Macro " + juce::String (i + 1) ? juce::String() : n;
        }
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
    beginPatchChange();
    seed = s;
    applyState (generator.generate (category, seed, currentState(), locks, arpGeneration, kickKeyGeneration));
    resetMacroNames();   // a new sound: the macros are named after what they now drive
    presetName = juce::String (categoryInfo()[(size_t) category].name) + " #" + juce::String::toHexString ((int) seed).toUpperCase().paddedLeft ('0', 6);
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
    applyState (generator.generateArpPattern (category, (uint32_t) random.nextInt(), currentState(), locks));
    if (! presetName.endsWith ("*"))
        presetName << "*";
    pushHistory();
    sendChangeMessage();
}

void PsyBoxProcessor::mutate()
{
    beginPatchChange();
    applyState (generator.mutate (mutateAmount, (uint32_t) random.nextInt(), currentState(), locks));
    if (! presetName.endsWith ("*"))
        presetName << "*";
    pushHistory();
    sendChangeMessage();
}

void PsyBoxProcessor::setLocked (P p, bool shouldLock)
{
    locks.set ((size_t) p, shouldLock);
    sendChangeMessage();
}

void PsyBoxProcessor::clearLocks()
{
    locks.reset();
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
    if (! canUndo()) return;
    beginPatchChange();
    // keep manual tweaks made since the last generator action reachable via redo
    history[(size_t) historyIndex].state = currentState();
    history[(size_t) historyIndex].macros = macroNames;
    --historyIndex;
    const auto& h = history[(size_t) historyIndex];
    if (h.category != category) saveSlot();
    seed = h.seed;
    category = h.category;
    presetName = h.name;
    macroNames = h.macros;
    applyState (h.state);
    restoreRanges();
    sendChangeMessage();
}

void PsyBoxProcessor::redo()
{
    if (! canRedo()) return;
    beginPatchChange();
    ++historyIndex;
    const auto& h = history[(size_t) historyIndex];
    if (h.category != category) saveSlot();
    seed = h.seed;
    category = h.category;
    presetName = h.name;
    macroNames = h.macros;
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
    macroNames[(size_t) i] = {};
    if (n == getMacroName (i) || n == "Macro " + juce::String (i + 1))
        n = {};
    macroNames[(size_t) i] = n;
    sendChangeMessage();
}

void PsyBoxProcessor::resetMacroNames()
{
    for (auto& n : macroNames) n = {};
}

void PsyBoxProcessor::writeMacroNames (juce::ValueTree& t) const
{
    for (int i = 0; i < kNumMacros; ++i)
        t.setProperty ("macro" + juce::String (i + 1), macroNames[(size_t) i], nullptr);
}

void PsyBoxProcessor::readMacroNames (const juce::ValueTree& t)
{
    // Older sessions stored "Macro 1" for a name nobody typed.
    for (int i = 0; i < kNumMacros; ++i)
    {
        const auto n = t.getProperty ("macro" + juce::String (i + 1), {}).toString().trim();
        macroNames[(size_t) i] = n == "Macro " + juce::String (i + 1) ? juce::String() : n;
    }
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
    beginPatchChange();
    applyState (ParamState());
    resetMacroNames();
    presetName = "Init";
    pushHistory();
    sendChangeMessage();
}

// ---------------------------------------------------------------------------
// Bounce
// ---------------------------------------------------------------------------

juce::File PsyBoxProcessor::bounceToFile()
{
    constexpr double fs = 48000.0;
    constexpr int block = 512;
    auto st = currentState();
    if (st.geti (P::EngineMode) == (int) EngineMode::Kick)
        st.set (P::ArpOn, 0.f);   // one hit: the WAV is a kick sample
    const double bpm = lastBpm.load();
    const int note = lastPlayedNote.load();

    // Gate: percussive patches (no sustain) only need attack + decay; sustained
    // ones are held for at least a beat so the body of the sound is there.
    const double attack = st.get (P::AenvA) / 1000.0, decay = st.get (P::AenvD) / 1000.0;
    const double beat = 60.0 / bpm;
    // Energy sounds are held for their whole length.
    const double gateSec = isEnergyCategory() ? juce::jlimit (0.1, 60.0, energyLengthSeconds())
                         : st.get (P::AenvS) < 0.05f ? juce::jlimit (0.1, 8.0, attack + decay)
                                                     : juce::jlimit (beat, 8.0, attack + decay + 0.25);
    const int gate = (int) (gateSec * fs);
    const int maxLen = (int) ((gateSec + 32.0) * fs);

    Engine e;
    e.setParams (st);   // before prepare, so the first note and the latency see this patch
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
        MidiEvent ev[2];
        int nEv = 0;
        if (pos == 0) { ev[nEv].type = MidiEvent::NoteOn; ev[nEv].number = note; ev[nEv].value = 0.8f; ++nEv; }
        if (gate >= pos && gate < pos + block) { ev[nEv].type = MidiEvent::NoteOff; ev[nEv].number = note; ev[nEv].offset = gate - pos; ++nEv; }
        e.process (bl.data(), br.data(), block, ev, nEv, bpm);
        float pk = 0.f;
        for (int i = 0; i < block; ++i)
        {
            L.push_back (bl[(size_t) i]);
            R.push_back (br[(size_t) i]);
            pk = std::max (pk, std::max (std::abs (bl[(size_t) i]), std::abs (br[(size_t) i])));
        }
        pos += block;
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

    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("PsyBox").getChildFile ("Bounces");
    dir.createDirectory();
    const auto noteName = juce::MidiMessage::getMidiNoteName (note, true, true, 4);
    const auto base = juce::File::createLegalFileName (presetName.trimCharactersAtEnd ("*") + " " + noteName);
    auto file = dir.getChildFile (base + ".wav");
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return {};
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr)
        return {};
    writer->writeFromAudioSampleBuffer (out, 0, len);
    writer.reset();
    return file;
}

// ---------------------------------------------------------------------------
// State & presets
// ---------------------------------------------------------------------------

void PsyBoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
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
    apvts.replaceState (tree);
    category = (Category) juce::jlimit (0, kNumCategories - 1, (int) tree.getProperty ("category", 0));
    seed = (uint32_t) (int) tree.getProperty ("seed", 0);
    mutateAmount = (float) tree.getProperty ("mutate", 0.25f);
    arpGeneration = (bool) tree.getProperty ("arpGen", false);
    kickKeyGeneration = (bool) tree.getProperty ("kickKeyGen", true);
    normaliseFamily();
    presetName = tree.getProperty ("presetName", "Init").toString();
    readMacroNames (tree);
    readSlots (tree);
    apvts.state.removeChild (apvts.state.getChildWithName ("Slots"), nullptr);
    locks.reset();
    for (const auto& id : juce::StringArray::fromTokens (tree.getProperty ("locks").toString(), ",", ""))
        if (const int idx = findParam (id.toStdString()); idx >= 0)
            locks.set ((size_t) idx);
    restoreRanges();
    history.clear();
    historyIndex = -1;
    pushHistory();
    sendChangeMessage();
}

void PsyBoxProcessor::normaliseFamily()
{
    const float want = (float) (isKickCategory() ? EngineMode::Kick : EngineMode::Synth);
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

bool PsyBoxProcessor::savePreset (const juce::String& name)
{
    auto state = apvts.copyState();
    state.setProperty ("category", (int) category, nullptr);
    state.setProperty ("seed", (int) seed, nullptr);
    writeMacroNames (state);
    auto xml = state.createXml();
    if (xml == nullptr) return false;
    const auto file = presetFolder().getChildFile (categoryInfo()[(size_t) category].name)
                                    .getChildFile (juce::File::createLegalFileName (name) + ".psyp");
    file.getParentDirectory().createDirectory();
    if (! xml->writeTo (file))
        return false;
    presetName = name;
    sendChangeMessage();
    return true;
}

bool PsyBoxProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (! adoptStateTag (xml.get(), apvts.state.getType()))
        return false;
    const auto tree = juce::ValueTree::fromXml (*xml);
    beginPatchChange();
    ParamState s;
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild (i);
        const int idx = findParam (child.getProperty ("id").toString().toStdString());
        if (idx >= 0)
            s.v[(size_t) idx] = paramDefs()[(size_t) idx].snap ((float) child.getProperty ("value"));
    }
    s.set (P::Quality, plain (P::Quality));
    s.set (P::MasterVol, plain (P::MasterVol));
    saveSlot();
    category = (Category) juce::jlimit (0, kNumCategories - 1, (int) tree.getProperty ("category", (int) category));
    seed = (uint32_t) (int) tree.getProperty ("seed", (int) seed);
    readMacroNames (tree);
    applyState (s);
    restoreRanges();
    presetName = file.getFileNameWithoutExtension();
    pushHistory();
    sendChangeMessage();
    return true;
}

void PsyBoxProcessor::stepPreset (int delta)
{
    const auto files = listPresets();
    if (files.isEmpty()) return;
    int idx = -1;
    for (int i = 0; i < files.size(); ++i)
        if (files[i].getFileNameWithoutExtension() == presetName) { idx = i; break; }
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
