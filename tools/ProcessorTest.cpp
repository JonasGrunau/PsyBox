// Self-checking tests of the JUCE processor (the layer psybox_tests can't
// reach): patch swaps, the keys it replays, host bypass, MIDI floods, the
// settings a patch change keeps, state round trips. Single-threaded and
// deterministic: the "message thread" calls run between blocks.
//   PSYSNAP_PLUGINTEST=1 psybox_snapshot     prints PASS/FAIL, exits 1 on a failure

#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace
{
using namespace psy;

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

struct Bench
{
    PsyBoxProcessor proc;
    juce::AudioBuffer<float> buf { 2, kBlock };
    std::vector<float> out;   // left channel, every block played

    // Like a VST3 host: a latency that moves restarts the plugin (prepareToPlay again).
    bool restartOnLatency = false;
    int restarts = 0;

    Bench() { proc.prepareToPlay (kRate, kBlock); }

    void set (P p, float plain) { proc.param (p)->setValueNotifyingHost (def (p).toNorm (plain)); }

    // plays n blocks; `midi` goes into the first one
    void play (int blocks, juce::MidiBuffer midi = {}, bool bypassed = false)
    {
        for (int b = 0; b < blocks; ++b)
        {
            const int latency = proc.getLatencySamples();
            if (bypassed) proc.processBlockBypassed (buf, midi);
            else proc.processBlock (buf, midi);
            midi.clear();
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + kBlock);
            if (restartOnLatency && proc.getLatencySamples() != latency)
            {
                proc.prepareToPlay (kRate, kBlock);
                ++restarts;
            }
        }
    }
    void play (int blocks, const juce::MidiMessage& m, bool bypassed = false)
    {
        juce::MidiBuffer midi;
        midi.addEvent (m, 0);
        play (blocks, midi, bypassed);
    }
    int blocksFor (double seconds) const { return (int) std::ceil (seconds * kRate / kBlock); }

    float rms (size_t from, size_t to) const
    {
        double s = 0.0;
        to = std::min (to, out.size());
        for (size_t i = from; i < to; ++i) s += (double) out[i] * out[i];
        return to > from ? (float) std::sqrt (s / (double) (to - from)) : 0.f;
    }
    float tailRms (double seconds) const { return rms (out.size() - (size_t) (seconds * kRate), out.size()); }

    // The pitch of the last `seconds`, from the first to the last rising zero crossing.
    float pitchHz (double seconds) const
    {
        const size_t from = out.size() - (size_t) (seconds * kRate);
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (size_t i = from + 1; i < out.size(); ++i)
            if (out[i - 1] < 0.f && out[i] >= 0.f)
            {
                const double at = (double) (i - 1) + out[i - 1] / (out[i - 1] - out[i]);
                if (first < 0.0) first = at;
                last = at;
                ++crossings;
            }
        return crossings > 1 ? (float) ((crossings - 1) * kRate / (last - first)) : 0.f;
    }

    // A sine on Osc A, nothing else: steps and clicks stand out against it.
    void sinePatch()
    {
        proc.initPatch();
        set (P::OscAShape, 0.f);
        set (P::F1Type, 0.f);
        set (P::F2Type, 0.f);
        play (blocksFor (0.1));   // the init swap
    }
};

int failures = 0;
void check (bool ok, const char* what)
{
    std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

void swapFromHostStateIsClean()
{
    Bench b;
    b.sinePatch();
    juce::MemoryBlock sine;
    b.proc.getStateInformation (sine);
    // the same sine from the FM oscillator: switched live, its phase jumps
    b.set (P::OscAType, (float) OscType::FM);
    b.set (P::OscAFm, 0.f);
    juce::MemoryBlock fm;
    b.proc.getStateInformation (fm);
    b.proc.setStateInformation (sine.getData(), (int) sine.getSize());
    b.play (b.blocksFor (0.1));

    b.play (b.blocksFor (0.3), juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    const size_t at = b.out.size();
    b.proc.setStateInformation (fm.getData(), (int) fm.getSize());   // a host preset, mid-note
    b.play (b.blocksFor (0.3));

    float peak = 0.f, step = 0.f;
    for (size_t i = at - 2000; i < at + 4000; ++i)
    {
        peak = std::max (peak, std::abs (b.out[i]));
        step = std::max (step, std::abs (b.out[i] - b.out[i - 1]));
    }
    // a 262 Hz sine moves at most 0.034 x its peak per sample; fades add a little
    check (peak > 0.01f && step < 0.06f * peak, "a host state load while a note plays swaps without a step");
    check (b.tailRms (0.1) > 0.01f, "the held key plays the loaded patch");
}

// Hold a key and press Generate again and again: every new sound plays, in
// every kind of category. In a DAW the held note used to go silent whenever
// one patch had the distortion and the next didn't (the latency moved, the
// host restarted the plugin), and a held kick never hit again.
void heldKeyPlaysEveryNewSound()
{
    bool allPlay = true, latencyStays = true;
    for (auto c : { Category::PsyLead, Category::Jilax, Category::KickFullOn, Category::OffbeatBass, Category::Phoscyon })
    {
        Bench b;
        b.restartOnLatency = true;
        b.proc.setCategory (c);
        b.play (b.blocksFor (0.2));
        b.play (b.blocksFor (0.3), juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100));
        b.restarts = 0;
        int silent = 0;
        for (int g = 0; g < 16; ++g)
        {
            b.proc.generate();
            const size_t at = b.out.size();
            b.play (b.blocksFor (0.3));
            if (b.rms (at + (size_t) (0.03 * kRate), b.out.size()) < 0.003f) ++silent;
        }
        if (silent > 0 || b.restarts > 0)
            std::printf ("  %s: %d of 16 silent, %d host restarts\n", psy::categoryInfo()[(size_t) c].name, silent, b.restarts);
        allPlay = allPlay && silent == 0;
        latencyStays = latencyStays && b.restarts == 0;
    }
    check (allPlay, "a key held through Generate plays every new sound (lead, shot, kick, bass, 303)");
    check (latencyStays, "and Generate never moves the latency (a VST3 host would restart the plugin)");
}

void allNotesOffForgetsTheKeys()
{
    Bench b;
    b.sinePatch();
    b.play (b.blocksFor (0.2), juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    b.play (b.blocksFor (0.5), juce::MidiMessage::allNotesOff (1));
    b.proc.initPatch();   // a swap replays the keys still down
    b.play (b.blocksFor (0.5));
    check (b.tailRms (0.2) < 1e-4f, "after CC 123 a patch swap brings no note back");
}

void sustainSurvivesASwap()
{
    Bench b;
    b.sinePatch();
    b.play (1, juce::MidiMessage::controllerEvent (1, 64, 127));
    b.play (b.blocksFor (0.2), juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    b.play (b.blocksFor (0.2), juce::MidiMessage::noteOff (1, 60));
    b.proc.initPatch();
    b.set (P::OscAShape, 0.f);
    b.play (b.blocksFor (0.5));
    check (b.tailRms (0.2) > 0.01f, "a note the pedal holds sounds on after a swap");
    b.play (b.blocksFor (1.0), juce::MidiMessage::controllerEvent (1, 64, 0));
    check (b.tailRms (0.2) < 1e-4f, "and lets go with the pedal");
}

void bypassFollowsTheKeys()
{
    Bench b;
    b.sinePatch();
    b.play (b.blocksFor (0.2), juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    b.play (b.blocksFor (0.2), juce::MidiMessage::noteOff (1, 60), true);   // released while bypassed
    b.play (b.blocksFor (0.5));
    check (b.tailRms (0.2) < 1e-4f, "a key let go during a bypass is silent after it");

    b.play (b.blocksFor (0.2), juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), true);   // pressed while bypassed
    b.play (b.blocksFor (0.3));
    check (b.tailRms (0.2) > 0.01f, "a key pressed during a bypass sounds after it");
}

void controllersNeverDropNoteOffs()
{
    Bench b;
    b.sinePatch();
    b.play (b.blocksFor (0.2), juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    juce::MidiBuffer flood;
    for (int i = 0; i < 3000; ++i)
        flood.addEvent (juce::MidiMessage::controllerEvent (1, 1, i % 128), i % kBlock);
    flood.addEvent (juce::MidiMessage::noteOff (1, 60), kBlock - 1);
    b.play (1, flood);
    b.play (b.blocksFor (1.0));
    check (b.tailRms (0.2) < 1e-4f, "a note-off after 3000 controllers in one block still lands");
}

void globalSettingsStay()
{
    Bench b;
    b.proc.generateWithSeed (5);
    b.set (P::MasterVol, -30.f);
    b.proc.initPatch();
    check (std::abs (b.proc.plain (P::MasterVol) + 30.f) < 0.05f, "Init keeps the master volume");
    b.proc.undo();
    check (std::abs (b.proc.plain (P::MasterVol) + 30.f) < 0.05f, "undo keeps the master volume");
    b.proc.redo();
    check (std::abs (b.proc.plain (P::MasterVol) + 30.f) < 0.05f, "redo keeps the master volume");
}

void redoKeepsTweaks()
{
    Bench b;
    b.proc.generateWithSeed (11);
    b.proc.generateWithSeed (12);
    b.proc.undo();
    b.set (P::F1Cutoff, 1234.f);   // tweak A
    b.proc.redo();
    b.proc.undo();
    check (std::abs (b.proc.plain (P::F1Cutoff) - 1234.f) < 1.f, "undo after redo comes back to the tweaked patch");
}

void slotMacroNamesSurviveASession()
{
    Bench a;
    a.proc.setCategory (Category::Acid);
    a.proc.setMacroName (0, "Wobble");
    a.proc.setCategory (Category::Atmos);   // Acid's patch goes into its slot
    juce::MemoryBlock state;
    a.proc.getStateInformation (state);

    Bench b;
    b.proc.setStateInformation (state.getData(), (int) state.getSize());
    b.proc.setCategory (Category::Acid);
    check (b.proc.getMacroName (0) == "Wobble", "a typed macro name in a category slot survives a session");
}

void stateRoundTrips()
{
    Bench a;
    a.proc.setCategory (Category::GhostStab);
    a.proc.generateWithSeed (0x2a2a);
    a.proc.setLocked (P::F1Cutoff, true);
    a.proc.setLocked (P::ReverbMix, true);
    a.set (P::FxOrder, (float) fxOrderCode ({ 3, 1, 2, 0, 4, 5, 6, 7, 8 }));
    a.proc.setMacroName (2, "Air");
    juce::MemoryBlock state;
    a.proc.getStateInformation (state);

    Bench b;
    b.proc.setStateInformation (state.getData(), (int) state.getSize());
    bool params = true;
    for (int i = 0; i < kNumParams; ++i)
        if (const float v = a.proc.plain ((P) i); std::abs (v - b.proc.plain ((P) i)) > 1e-5f * std::max (1.f, std::abs (v)))   // a ulp or two through norm
        {
            std::printf ("  %s: %g -> %g\n", paramDefs()[(size_t) i].id, a.proc.plain ((P) i), b.proc.plain ((P) i));
            params = false;
        }
    check (params, "a session restores every parameter");
    check (b.proc.getCategory() == Category::GhostStab && b.proc.getSeed() == 0x2a2a, "and the category and seed");
    check (b.proc.isLocked (P::F1Cutoff) && b.proc.isLocked (P::ReverbMix) && b.proc.numLocks() == 2, "and the locks");
    check (b.proc.getPresetName() == a.proc.getPresetName() && b.proc.getMacroName (2) == "Air", "and the name and macro names");
}

// auval sets values and reads them back: a continuous parameter returns
// exactly what the host set, and the engine still plays its plain value.
void hostValuesComeBackExactly()
{
    Bench b;
    bool exact = true, played = true;
    for (int i = 0; i < kNumParams; ++i)
    {
        auto* prm = b.proc.param ((P) i);
        const auto& d = paramDefs()[(size_t) i];
        const bool continuous = d.mapping != Mapping::Choice && d.mapping != Mapping::Toggle;
        for (const float n : { 0.f, 1.f, prm->getDefaultValue(), 0.3711f })
        {
            prm->setValueNotifyingHost (n);
            if (continuous && prm->getValue() != n)
            {
                std::printf ("  %s: set %.9g, read %.9g\n", d.id, (double) n, (double) prm->getValue());
                exact = false;
            }
            if (const float want = d.snap (d.toPlain (n)); std::abs (b.proc.plain ((P) i) - want) > 1e-6f * std::max (1.f, std::abs (want)))
            {
                std::printf ("  %s: norm %g plays %g, not %g\n", d.id, (double) n, (double) b.proc.plain ((P) i), (double) want);
                played = false;
            }
        }
    }
    check (exact, "every continuous parameter gives the host back the value it set");
    check (played, "and the engine plays that value");
}

// The WAV bounce renders on another thread: it writes the file, stops fast
// when cancelled (and leaves nothing), and keys the sound it rendered.
void bounceRendersOffTheMessageThread()
{
    Bench b;
    b.sinePatch();
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("psybox-bounce-test");
    dir.createDirectory();

    const auto req = b.proc.bounceRequest();
    const auto file = dir.getChildFile ("sine.wav");
    std::atomic<float> progress { 0.f };
    bool ok = false;
    std::thread t ([&] { ok = PsyBoxProcessor::renderBounce (req, file, [] { return false; }, &progress); });
    t.join();
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    float peak = 0.f;
    juce::int64 len = 0;
    if (std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file)); r != nullptr)
    {
        len = r->lengthInSamples;
        juce::Range<float> ranges[2];
        r->readMaxLevels (0, len, ranges, 2);
        peak = std::max (ranges[0].getEnd(), -ranges[0].getStart());
    }
    check (ok && len > 4800 && std::abs (juce::Decibels::gainToDecibels (peak) - PsyBoxProcessor::kBouncePeakDb) < 0.05f
               && progress.load() == 1.f,
           "a bounce renders on another thread to a WAV peaking at -0.1 dBFS, like the Jilax packs");

    b.set (P::F1Cutoff, 900.f);
    check (b.proc.bounceRequest().key != req.key && b.proc.bounceRequest().key == b.proc.bounceRequest().key,
           "and its key follows the sound");

    b.proc.setCategory (Category::Riser);
    b.proc.generateWithSeed (7);
    b.set (P::EnergyMode, (float) EnergyMode::Time);
    b.set (P::EnergySec, 60.f);
    const auto longReq = b.proc.bounceRequest();
    const auto longFile = dir.getChildFile ("riser.wav");
    std::atomic<bool> stop { false };
    bool longOk = true;
    const auto t0 = std::chrono::steady_clock::now();
    std::thread t2 ([&] { longOk = PsyBoxProcessor::renderBounce (longReq, longFile, [&] { return stop.load(); }); });
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    stop = true;
    t2.join();
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    check (! longOk && ! longFile.exists() && secs < 1.0, "a cancelled bounce of a minute-long riser stops at once and leaves no file");
    dir.deleteRecursively();
}

// The playback key: every key plays one note, and both exports render it.
void playbackKeyPlaysOneNote()
{
    Bench b;
    b.sinePatch();
    b.set (P::AenvR, 10.f);
    const auto hz = [] (int note) { return 440.f * std::exp2 ((float) (note - 69) / 12.f); };
    const auto near = [] (float a, float want) { return std::abs (a - want) < 0.01f * want; };

    b.proc.setPlayKey (1, 3);   // C#3
    b.play (b.blocksFor (0.1));
    b.play (b.blocksFor (0.3), juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    const float first = b.pitchHz (0.2);
    b.play (b.blocksFor (0.3), juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100));   // a second key: the note again
    const float second = b.pitchHz (0.2);
    b.play (b.blocksFor (0.3), juce::MidiMessage::noteOff (1, 60));
    const float held = b.tailRms (0.1);   // 72 is still down
    b.play (b.blocksFor (0.3), juce::MidiMessage::noteOff (1, 72));
    std::printf ("  C#3: %.1f and %.1f Hz (want %.1f)\n", first, second, hz (49));
    check (near (first, hz (49)) && near (second, hz (49)), "with a playback key every key plays that note (C#3)");
    check (held > 0.01f && b.tailRms (0.1) < 1e-4f, "and it ends when the last key is let go");

    b.play (b.blocksFor (0.3), juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100));
    b.proc.setPlayKey (4, 2);   // E2, while the key is held
    b.play (b.blocksFor (0.3));
    const float moved = b.pitchHz (0.2);
    check (near (moved, hz (40)) && b.tailRms (0.1) > 0.01f, "a new playback key moves the held note to it");
    check (b.proc.bounceRequest().note == 40, "and the WAV export renders it");
    b.play (b.blocksFor (0.3), juce::MidiMessage::noteOff (1, 64));
    check (b.tailRms (0.1) < 1e-4f, "and the key lets it go");

    b.proc.setPlayKey (-1, 2);
    b.play (b.blocksFor (0.1));
    b.play (b.blocksFor (0.3), juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    check (near (b.pitchHz (0.2), hz (60)) && b.proc.getPlayOctave() == 2, "off, the keys play themselves again");

    juce::MemoryBlock state;
    b.proc.setPlayKey (11, 5);
    b.proc.getStateInformation (state);
    Bench c;
    c.proc.setStateInformation (state.getData(), (int) state.getSize());
    check (c.proc.getPlayKey() == 11 && c.proc.getPlayOctave() == 5 && c.proc.getPlayNote() == 83, "the session keeps the playback key");
}

// A batch renders every sound off the message thread, in order and named
// after its seed, and a seed typed back gives the same sound.
void batchRendersEverySound()
{
    Bench b;
    b.proc.setCategory (Category::Pluck);
    b.proc.setPlayKey (9, 3);   // A3
    b.play (b.blocksFor (0.1));
    const auto seed = b.proc.getSeed();
    const auto name = b.proc.getPresetName();
    b.proc.startBatch (4);
    const auto folder = b.proc.getBatchFolder();
    const auto t0 = std::chrono::steady_clock::now();
    while (b.proc.getBatchStatus().running && std::chrono::steady_clock::now() - t0 < std::chrono::seconds (60))
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    const auto st = b.proc.getBatchStatus();
    const auto files = b.proc.getBatchFiles();
    std::printf ("  4 plucks in %.2f s\n", std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    check (! st.running && st.finished == 4 && st.failed == 0 && files.size() == 4 && st.progress == 1.f,
           "a batch renders every sound on worker threads");

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const auto read = [&fm] (const juce::File& f) {
        juce::AudioBuffer<float> buf;
        if (std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f)); r != nullptr)
        {
            buf.setSize ((int) r->numChannels, (int) r->lengthInSamples);
            r->read (&buf, 0, buf.getNumSamples(), 0, true, true);
        }
        return buf;
    };
    bool named = true, levelled = true;
    for (int i = 0; i < files.size(); ++i)
    {
        const juce::File f (files[i]);
        named = named && f.getParentDirectory() == folder && f.getFileName().startsWith ("0" + juce::String (i + 1) + " Pluck ")
                && f.getFileName().endsWith (" A3.wav");
        const auto buf = read (f);
        levelled = levelled && buf.getNumSamples() > 0
                   && std::abs (juce::Decibels::gainToDecibels (buf.getMagnitude (0, buf.getNumSamples())) - PsyBoxProcessor::kBouncePeakDb) < 0.05f;
    }
    check (named && levelled, "in order, named after seed and playback key, each peaking at -0.1 dBFS");
    check (b.proc.getSeed() == seed && b.proc.getPresetName() == name, "and the sound playing stays");

    // the second file's seed, typed into SEED, plays that file's sound again
    const auto second = juce::File (files[1]).getFileNameWithoutExtension();
    const auto words = juce::StringArray::fromTokens (second, " ", "");
    const auto hex = words[words.size() - 2];   // "02 Pluck 3F2A91 A3"
    b.proc.generateWithSeed ((uint32_t) hex.getHexValue32());
    const auto again = folder.getChildFile ("again.wav");
    PsyBoxProcessor::renderBounce (b.proc.bounceRequest(), again, [] { return false; });
    // (the plugin's parameters hold the patch through their normalised values,
    // a few ulps from the generator's, so the two agree to within rounding)
    const auto x = read (juce::File (files[1])), y = read (again);
    double diff = 0.0, sig = 0.0;
    const int len = std::min (x.getNumSamples(), y.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < len; ++i)
        {
            const double d = (double) x.getSample (ch, i) - y.getSample (ch, i);
            diff += d * d;
            sig += (double) x.getSample (ch, i) * x.getSample (ch, i);
        }
    const double diffDb = 10.0 * std::log10 ((diff + 1e-30) / (sig + 1e-30));
    std::printf ("  seed %s: %d vs %d samples, difference %.1f dB\n", hex.toRawUTF8(), x.getNumSamples(), y.getNumSamples(), diffDb);
    check (len > 0 && std::abs (x.getNumSamples() - y.getNumSamples()) <= 480 && diffDb < -60.0,
           "a batch file's seed typed into SEED gives that sound back");

    b.proc.startBatch (60);
    const auto cancelled = b.proc.getBatchFolder();
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    const auto t1 = std::chrono::steady_clock::now();
    b.proc.cancelBatch();
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count();
    check (secs < 1.0 && b.proc.getBatchStatus().total == 0, "cancelling a batch of 60 stops at once");
    folder.deleteRecursively();
    cancelled.deleteRecursively();
}

// Two PsyBox windows open at once each draw in their own category's colour,
// not in the colour of the one that opened (or changed) last.
void twoWindowsKeepTheirAccents()
{
    PsyBoxProcessor lead, bass;
    lead.setCategory (Category::PsyLead);
    bass.setCategory (Category::PsyBass);
    std::unique_ptr<juce::AudioProcessorEditor> leadWin (lead.createEditor()), bassWin (bass.createEditor());   // the bass opens last
    const auto colourOf = [] (Category k) { return juce::Colour ((juce::uint32) (0xff000000 | categoryInfo()[(size_t) k].colour)); };
    // pixels drawn exactly in colour c (anti-aliased edges blend, solid strokes don't)
    const auto count = [] (juce::Component& win, juce::Colour c) {
        const auto img = win.createComponentSnapshot (win.getLocalBounds(), true, 1.f);
        const juce::Image::BitmapData px (img, juce::Image::BitmapData::readOnly);
        int n = 0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
                n += px.getPixelColour (x, y) == c;
        return n;
    };
    const int leadInLead = count (*leadWin, colourOf (Category::PsyLead)), bassInLead = count (*leadWin, colourOf (Category::PsyBass));
    const int bassInBass = count (*bassWin, colourOf (Category::PsyBass)), leadInBass = count (*bassWin, colourOf (Category::PsyLead));
    std::printf ("  lead window: %d lead, %d bass pixels; bass window: %d bass, %d lead\n", leadInLead, bassInLead, bassInBass, leadInBass);
    check (leadInLead > 50 && bassInLead == 0 && bassInBass > 50 && leadInBass == 0, "two open windows each draw in their own category's colour");
}

void renderLatencyFromTheStart()
{
    Bench b;
    b.set (P::Quality, 0.f);
    b.set (P::RenderQuality, 2.f);
    b.proc.setNonRealtime (true);
    b.proc.prepareToPlay (kRate, kBlock);
    const int before = b.proc.getLatencySamples();
    b.play (2);
    check (before > 0 && b.proc.getLatencySamples() == before, "an offline render reports its latency at prepare");
}

void tailCoversTheEffects()
{
    Bench b;
    b.proc.initPatch();
    b.set (P::ReverbOn, 1.f);
    b.set (P::ReverbDecay, 12.f);
    check (b.proc.getTailLengthSeconds() >= 12.0, "the tail covers a 12 s reverb");
}
} // namespace

int runProcessorTests()
{
    swapFromHostStateIsClean();
    heldKeyPlaysEveryNewSound();
    playbackKeyPlaysOneNote();
    batchRendersEverySound();
    allNotesOffForgetsTheKeys();
    sustainSurvivesASwap();
    bypassFollowsTheKeys();
    controllersNeverDropNoteOffs();
    globalSettingsStay();
    redoKeepsTweaks();
    slotMacroNamesSurviveASession();
    stateRoundTrips();
    hostValuesComeBackExactly();
    bounceRendersOffTheMessageThread();
    twoWindowsKeepTheirAccents();
    renderLatencyFromTheStart();
    tailCoversTheEffects();
    std::printf ("%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
