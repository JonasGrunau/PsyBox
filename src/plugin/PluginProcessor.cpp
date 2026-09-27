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
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PsySynthProcessor::createLayout()
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
            layout.add (std::make_unique<juce::AudioParameterChoice> (id, name, items, (int) d.def));
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

PsySynthProcessor::PsySynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PsySynth", createLayout())
{
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

bool PsySynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PsySynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (currentState());
    engine.prepare (sampleRate, samplesPerBlock);
    reportedLatency = engine.latencySamples();
    setLatencySamples (reportedLatency);
}

ParamState PsySynthProcessor::currentState() const
{
    ParamState s;
    for (size_t i = 0; i < (size_t) kNumParams; ++i)
        s.v[i] = rawValues[i]->load (std::memory_order_relaxed);
    return s;
}

void PsySynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    for (size_t i = 0; i < (size_t) kNumParams; ++i)
        audioParams.v[i] = rawValues[i]->load (std::memory_order_relaxed);
    engine.setParams (audioParams);

    midiEvents.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        MidiEvent e;
        e.offset = juce::jlimit (0, n - 1, meta.samplePosition);
        if (m.isNoteOn()) { e.type = MidiEvent::NoteOn; e.number = m.getNoteNumber(); e.value = m.getFloatVelocity(); }
        else if (m.isNoteOff()) { e.type = MidiEvent::NoteOff; e.number = m.getNoteNumber(); }
        else if (m.isPitchWheel()) { e.type = MidiEvent::PitchBend; e.value = (float) (m.getPitchWheelValue() - 8192) / 8192.f; }
        else if (m.isController()) { e.type = MidiEvent::Controller; e.number = m.getControllerNumber(); e.value = (float) m.getControllerValue() / 127.f; }
        else if (m.isChannelPressure()) { e.type = MidiEvent::ChannelPressure; e.value = (float) m.getChannelPressureValue() / 127.f; }
        else if (m.isAftertouch()) { e.type = MidiEvent::ChannelPressure; e.value = (float) m.getAfterTouchValue() / 127.f; }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { e.type = MidiEvent::AllNotesOff; }
        else continue;
        if (midiEvents.size() < midiEvents.capacity())
            midiEvents.push_back (e);
    }

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto b = pos->getBpm())
                bpm = *b;

    buffer.clear();
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;
    engine.process (l, r, n, midiEvents.data(), (int) midiEvents.size(), bpm);

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
    peakLevel.store (std::max (engine.lastPeak(), peakLevel.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    activeVoices.store (engine.activeVoices(), std::memory_order_relaxed);
}

void PsySynthProcessor::copyScope (float* dest, int num) const noexcept
{
    const int w = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < num; ++i)
        dest[i] = scope[(size_t) ((w - num + i) & (kScopeSize - 1))];
}

// ---------------------------------------------------------------------------
// Generator
// ---------------------------------------------------------------------------

void PsySynthProcessor::applyState (const ParamState& s)
{
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

void PsySynthProcessor::setCategory (Category c)
{
    category = c;
    sendChangeMessage();
}

void PsySynthProcessor::generate()
{
    generateWithSeed ((uint32_t) random.nextInt() & 0xFFFFFFu);
}

void PsySynthProcessor::generateWithSeed (uint32_t s)
{
    seed = s;
    applyState (generator.generate (category, seed, currentState(), locks));
    presetName = juce::String (categoryInfo()[(size_t) category].name) + " #" + juce::String::toHexString ((int) seed).toUpperCase().paddedLeft ('0', 6);
    pushHistory();
    sendChangeMessage();
}

void PsySynthProcessor::mutate()
{
    applyState (generator.mutate (mutateAmount, (uint32_t) random.nextInt(), currentState(), locks));
    if (! presetName.endsWith ("*"))
        presetName << "*";
    pushHistory();
    sendChangeMessage();
}

void PsySynthProcessor::setLocked (P p, bool shouldLock)
{
    locks.set ((size_t) p, shouldLock);
    sendChangeMessage();
}

void PsySynthProcessor::clearLocks()
{
    locks.reset();
    sendChangeMessage();
}

void PsySynthProcessor::pushHistory()
{
    if (historyIndex + 1 < (int) history.size())
        history.erase (history.begin() + historyIndex + 1, history.end());
    history.push_back ({ currentState(), seed, category, presetName });
    if (history.size() > 64)
        history.erase (history.begin());
    historyIndex = (int) history.size() - 1;
}

void PsySynthProcessor::undo()
{
    if (! canUndo()) return;
    // keep manual tweaks made since the last generator action reachable via redo
    history[(size_t) historyIndex].state = currentState();
    --historyIndex;
    const auto& h = history[(size_t) historyIndex];
    seed = h.seed;
    category = h.category;
    presetName = h.name;
    applyState (h.state);
    restoreRanges();
    sendChangeMessage();
}

void PsySynthProcessor::redo()
{
    if (! canRedo()) return;
    ++historyIndex;
    const auto& h = history[(size_t) historyIndex];
    seed = h.seed;
    category = h.category;
    presetName = h.name;
    applyState (h.state);
    restoreRanges();
    sendChangeMessage();
}

// Mutate needs the recipe's ranges; they are a pure function of category and
// seed, so re-run the recipe on a scratch patch instead of storing them.
void PsySynthProcessor::restoreRanges()
{
    Generator scratch;
    scratch.generate (category, seed, ParamState(), LockSet());
    generator.setRanges (scratch.lastRanges());
}

void PsySynthProcessor::initPatch()
{
    applyState (ParamState());
    presetName = "Init";
    pushHistory();
    sendChangeMessage();
}

// ---------------------------------------------------------------------------
// State & presets
// ---------------------------------------------------------------------------

void PsySynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("category", (int) category, nullptr);
    state.setProperty ("seed", (int) seed, nullptr);
    state.setProperty ("mutate", mutateAmount, nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    juce::StringArray lockIds;
    for (int i = 0; i < kNumParams; ++i)
        if (locks.test ((size_t) i))
            lockIds.add (paramDefs()[(size_t) i].id);
    state.setProperty ("locks", lockIds.joinIntoString (","), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PsySynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);
    category = (Category) juce::jlimit (0, kNumCategories - 1, (int) tree.getProperty ("category", 0));
    seed = (uint32_t) (int) tree.getProperty ("seed", 0);
    mutateAmount = (float) tree.getProperty ("mutate", 0.25f);
    presetName = tree.getProperty ("presetName", "Init").toString();
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

juce::File PsySynthProcessor::presetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("PsySynth").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> PsySynthProcessor::listPresets() const
{
    auto files = presetFolder().findChildFiles (juce::File::findFiles, true, "*.psyp");
    files.sort();
    return files;
}

bool PsySynthProcessor::savePreset (const juce::String& name)
{
    auto state = apvts.copyState();
    state.setProperty ("category", (int) category, nullptr);
    state.setProperty ("seed", (int) seed, nullptr);
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

bool PsySynthProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;
    const auto tree = juce::ValueTree::fromXml (*xml);
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
    category = (Category) juce::jlimit (0, kNumCategories - 1, (int) tree.getProperty ("category", (int) category));
    seed = (uint32_t) (int) tree.getProperty ("seed", (int) seed);
    applyState (s);
    restoreRanges();
    presetName = file.getFileNameWithoutExtension();
    pushHistory();
    sendChangeMessage();
    return true;
}

void PsySynthProcessor::stepPreset (int delta)
{
    const auto files = listPresets();
    if (files.isEmpty()) return;
    int idx = -1;
    for (int i = 0; i < files.size(); ++i)
        if (files[i].getFileNameWithoutExtension() == presetName) { idx = i; break; }
    idx = idx < 0 ? (delta > 0 ? 0 : files.size() - 1) : (idx + delta + files.size()) % files.size();
    loadPreset (files[idx]);
}

juce::AudioProcessorEditor* PsySynthProcessor::createEditor()
{
    return new PsySynthEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsySynthProcessor();
}
