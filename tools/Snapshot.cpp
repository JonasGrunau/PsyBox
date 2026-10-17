// Renders the editor offscreen to PNGs, one per requested category, so the UI
// can be reviewed without a DAW or screen-recording permission.
//   psybox_snapshot <out-dir> [category-index ...]
// Setting PSYSNAP_TAB=n also switches every tab strip to tab n (clamped),
// and PSYSNAP_LOCKS=1 locks a couple of parameters, to review those states.
// PSYSNAP_VIEW=n opens view n (0 Osc/Kick, 1 FX, 2 Matrix, 3 Arp, 4 Global).
// PSYSNAP_MODTAB=n opens page n of the modulators (0 Amp Env ... 6 LFO 3).
// PSYSNAP_DRAG=1 shows the knobs as they look while a mod source is dragged.
// PSYSNAP_DROP="7:oscA_level,15:delay_mix" drops mod source 7 (LFO 3) and 15
// (Macro 2) on those knobs, through the knobs' own drop handling.
// PSYSNAP_DNDTEST=1 drives the editor through JUCE without a window (no OS
// events): drags LFO 2 onto Osc A's level, then the new ring upwards, then the
// FX chain, and prints PASS/FAIL for each step (plus the macro names).
// PSYSNAP_PLUGINTEST=1 runs the processor checks in ProcessorTest.cpp (no
// editor) and exits 1 on a failure.
// PSYSNAP_KEYS=1 renders the standalone's keyboard strip under the panels.
// PSYSNAP_ARP=1 generates with an arp.
// PSYSNAP_SCALE=1 renders at 1x (default 2x).
// PSYSNAP_PARAMS="comp_on=1,comp_mode=0" sets plain parameter values after Generate.
// PSYSNAP_UNISON=1 opens Osc A's unison settings (the gear), =2 holds Osc A's
// voice lines up as if the mouse were on its UNISON field.
// PSYSNAP_SWAPTEST=<out.f32> plays a held key through the first category's
// arp in real time on a second thread while Generate runs twice from this one
// (the way a user hits it mid-line), writes the mono output as float32 and
// prints where each Generate came in. No editor.
// PSYSNAP_STRIP=n scrolls the Phoscyon FX rack to effect n (use with
// PSYSNAP_VIEW=1), e.g. 10 for the Phoscyon Limiter.
// PSYSNAP_CHAINDRAG=n holds the FX chain's first row dragged to row n (use
// with PSYSNAP_VIEW=1): the rows make room and its landing place stays empty.

#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"
#include "ui/FxPage.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <typeinfo>

int runProcessorTests();   // ProcessorTest.cpp: PSYSNAP_PLUGINTEST=1

static void selectTabs (juce::Component& c, int tab)
{
    for (auto* child : c.getChildren())
    {
        if (dynamic_cast<Sidebar*> (child) != nullptr)
            continue;   // its tabs pick the family list, not a page
        if (auto* t = dynamic_cast<TabStrip*> (child))
        {
            t->setSelected (tab);
            if (t->onChange) t->onChange (t->getSelected());
        }
        selectTabs (*child, tab);
    }
}

static ParamKnob* findKnob (juce::Component& c, psy::P p)
{
    for (auto* child : c.getChildren())
    {
        if (auto* k = dynamic_cast<ParamKnob*> (child); k != nullptr && k->getParam() == p)
            return k;
        if (auto* k = findKnob (*child, p))
            return k;
    }
    return nullptr;
}

static void showModPage (juce::Component& c, int page)
{
    for (auto* child : c.getChildren())
    {
        if (auto* m = dynamic_cast<ModPanel*> (child))
            m->show (page);
        showModPage (*child, page);
    }
}

template <typename T>
static T* findChild (juce::Component& c, std::function<bool (T&)> match)
{
    for (auto* child : c.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (child); t != nullptr && t->isVisible() && match (*t))
            return t;
        if (auto* t = findChild<T> (*child, match))
            return t;
    }
    return nullptr;
}

static int runSwapTest (PsyBoxProcessor& proc, psy::Category c, const char* path)
{
    constexpr int block = 256, rate = 48000;
    proc.setArpGeneration (true);
    proc.setCategory (c);
    proc.generateWithSeed (0x51u);
    proc.prepareToPlay (rate, block);
    const int blocks = rate * 4 / block;
    std::vector<float> out ((size_t) (blocks * block));
    std::atomic<int> played { 0 };
    std::thread audio ([&] {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        const auto start = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            midi.clear();
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
            proc.processBlock (buf, midi);
            std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + block, out.begin() + (std::ptrdiff_t) b * block);
            played = (b + 1) * block;
            std::this_thread::sleep_until (start + std::chrono::microseconds ((int64_t) (b + 1) * block * 1000000 / rate));
        }
    });
    for (double t : { 1.0, 2.3 })
    {
        while (played.load() < (int) (t * rate))
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        std::printf ("generate at %d\n", played.load());
        proc.generate();
    }
    audio.join();
    juce::File::getCurrentWorkingDirectory().getChildFile (path).replaceWithData (out.data(), out.size() * sizeof (float));
    return 0;
}

// Drives the knob the way a drop and a ring drag do, with synthetic mouse
// events sent to the components (no window: macOS won't show one for a
// console tool, and JUCE's drag container itself is stock).
static int runDragAndDropTest (PsyBoxProcessor& proc, juce::AudioProcessorEditor& editor)
{
    int failures = 0;
    const auto check = [&] (bool ok, const juce::String& what) {
        std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        failures += ok ? 0 : 1;
    };
    auto* handle = findChild<ModSourceHandle> (editor, [] (ModSourceHandle& h) { return h.getSource() == psy::ModSrc::Lfo2; });
    auto* knob = findChild<ParamKnob> (editor, [] (ParamKnob& k) { return k.getParam() == psy::P::OscALevel; });
    auto* blend = findChild<ParamKnob> (editor, [] (ParamKnob& k) { return k.getParam() == psy::P::OscABlend; });
    auto* semi = findChild<NumberBox> (editor, [] (NumberBox& b) { return b.getParam() == psy::P::OscACoarse; });
    if (handle == nullptr || knob == nullptr || blend == nullptr || semi == nullptr) { std::printf ("FAIL controls not found\n"); return 1; }

    // drop
    const juce::DragAndDropTarget::SourceDetails d (moddrag::describe (handle->getSource()), handle, {});
    check (knob->isInterestedInDragSource (d), "Osc A level takes a dragged LFO");
    check (! blend->isInterestedInDragSource (d), "Osc A blend (no mod target) refuses it");
    check (semi->isInterestedInDragSource (d), "Osc A's SEM field takes it (Pitch A)");
    semi->itemDropped (d);
    check (proc.findModSlot (psy::ModSrc::Lfo2, psy::ModDst::PitchA) >= 0, "the drop on SEM added LFO 2 -> Pitch A");
    knob->itemDropped (d);
    const int slot = proc.findModSlot (psy::ModSrc::Lfo2, psy::ModDst::LevelA);
    check (slot >= 0, "the drop added LFO 2 -> Level A in slot " + juce::String (slot));
    knob->itemDropped (d);
    check (proc.findModSlot (psy::ModSrc::Lfo2, psy::ModDst::LevelA) == slot,
           "dropping it again reuses the route");
    if (slot < 0) return failures;

    // ring drag: grab between body and track, pull up
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto event = [&] (juce::Point<float> pos, juce::Point<float> downPos, bool dragged) {
        return juce::MouseEvent (src, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 0.f, 0.f, 0.f, 0.f, 0.f,
                                 knob, knob, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1, dragged);
    };
    knob->mouseMove (event ({}, {}, false));   // refresh
    const float level = proc.plain (psy::P::OscALevel);
    const float before = proc.plain (psy::modAmtParam (slot));
    const juce::Point<float> ring { knob->getWidth() * 0.5f - 15.5f, 20.f };
    knob->mouseDown (event (ring, ring, false));
    for (int i = 1; i <= 8; ++i)
        knob->mouseDrag (event (ring.translated (0.f, -5.f * (float) i), ring, true));
    knob->mouseUp (event (ring.translated (0.f, -40.f), ring, true));
    const float after = proc.plain (psy::modAmtParam (slot));
    check (after > before + 0.2f, "dragging the ring up raised the amount " + juce::String (before, 2) + " -> " + juce::String (after, 2));
    check (std::abs (proc.plain (psy::P::OscALevel) - level) < 1e-4f, "the knob's own value stayed put");
    check (proc.uiSelectedSource == psy::ModSrc::Lfo2, "LFO 2 is the selected source");
    // FX chain: drag Distortion (row 1) below Crush and Freq Shift, then click a row
    if (auto* ed = dynamic_cast<PsyBoxEditor*> (&editor))
    {
        ed->showView (View::Fx);
        auto* chain = findChild<FxPage::Chain> (editor, [] (FxPage::Chain&) { return true; });
        check (chain != nullptr, "the FX chain is on the FX page");
        if (chain != nullptr)
        {
            const auto at = [&] (juce::Point<float> pos, juce::Point<float> down, bool dragged) {
                return juce::MouseEvent (src, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 0.f, 0.f, 0.f, 0.f, 0.f,
                                         chain, chain, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
            };
            constexpr float top = 48.f, rowH = 30.f;   // kPad + kHeaderH + kHeaderGap, Chain::kRowH
            const juce::Point<float> row1 { 60.f, top + rowH * 0.5f }, row3 { 60.f, top + rowH * 2.5f };
            chain->mouseDown (at (row1, row1, false));
            chain->mouseDrag (at (row1.translated (0.f, 20.f), row1, true));
            chain->mouseDrag (at (row3, row1, true));
            chain->mouseUp (at (row3, row1, true));
            const auto order = psy::fxChain ((int) proc.plain (psy::P::FxOrder));
            check (order == psy::FxChain { 1, 2, 0, 3, 4, 5, 6, 7, 8 }, "dragging Distortion two rows down moved it in the chain");
            const bool crushWas = proc.plain (psy::P::CrushOn) > 0.5f;
            chain->mouseDown (at (row1, row1, false));
            chain->mouseUp (at (row1, row1, false));
            check ((proc.plain (psy::P::CrushOn) > 0.5f) != crushWas, "a click on row 1 (now Crush) switched it");
        }
    }
    std::printf ("macro names: %s / %s / %s / %s\n", proc.getMacroName (0).toRawUTF8(), proc.getMacroName (1).toRawUTF8(),
                 proc.getMacroName (2).toRawUTF8(), proc.getMacroName (3).toRawUTF8());
    return failures;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? juce::String (argv[1]) : juce::String());
    outDir.createDirectory();

    std::vector<int> cats;
    for (int i = 2; i < argc; ++i) cats.push_back (juce::String (argv[i]).getIntValue());
    if (cats.empty()) cats = { 0 };

    PsyBoxEditor::forceKeyboard = std::getenv ("PSYSNAP_KEYS") != nullptr;
    if (std::getenv ("PSYSNAP_PLUGINTEST") != nullptr)
        return runProcessorTests();
    PsyBoxProcessor proc;
    if (auto* swapOut = std::getenv ("PSYSNAP_SWAPTEST"))
        return runSwapTest (proc, (psy::Category) juce::jlimit (0, psy::kNumCategories - 1, cats[0]), swapOut);
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    // On the desktop (a window nobody looks at) the editor counts as showing, so
    // the panels' timers keep their layout current, as they do on screen.
    editor->setVisible (true);
    editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);

    proc.setArpGeneration (std::getenv ("PSYSNAP_ARP") != nullptr);
    for (int c : cats)
    {
        proc.setCategory ((psy::Category) juce::jlimit (0, psy::kNumCategories - 1, c));
        proc.generateWithSeed (0x1000u + (uint32_t) c * 7919u);
        if (auto* list = std::getenv ("PSYSNAP_PARAMS"))
            for (const auto& item : juce::StringArray::fromTokens (list, ",", ""))
            {
                const int i = psy::findParam (item.upToFirstOccurrenceOf ("=", false, false).trim().toStdString());
                if (i >= 0)
                    proc.param ((psy::P) i)->setValueNotifyingHost (
                        psy::paramDefs()[(size_t) i].toNorm (item.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
            }

        // Feed a note through the engine so the scope has something to show.
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        for (int b = 0; b < 40; ++b)
        {
            proc.processBlock (buf, midi);
            midi.clear();
        }
        if (auto* locks = std::getenv ("PSYSNAP_LOCKS"); locks != nullptr && *locks == '1')
        {
            proc.setLocked (psy::P::F1Cutoff, true);
            proc.setLocked (psy::P::AenvD, true);
        }
        if (auto* tab = std::getenv ("PSYSNAP_TAB"))
            selectTabs (*editor, juce::String (tab).getIntValue());
        if (auto* drops = std::getenv ("PSYSNAP_DROP"))
            for (const auto& item : juce::StringArray::fromTokens (drops, ",", ""))
            {
                const auto src = (psy::ModSrc) item.upToFirstOccurrenceOf (":", false, false).getIntValue();
                const int i = psy::findParam (item.fromFirstOccurrenceOf (":", false, false).trim().toStdString());
                if (auto* k = i >= 0 ? findKnob (*editor, (psy::P) i) : nullptr)
                {
                    const juce::DragAndDropTarget::SourceDetails d (moddrag::describe (src), nullptr, {});
                    if (k->isInterestedInDragSource (d)) k->itemDropped (d);
                    else std::printf ("knob %s does not take source %d\n", item.toRawUTF8(), (int) src);
                }
            }
        if (auto* v = std::getenv ("PSYSNAP_VIEW"))
            if (auto* ed = dynamic_cast<PsyBoxEditor*> (editor.get()))
                ed->showView ((View) juce::jlimit (0, (int) View::Count - 1, juce::String (v).getIntValue()));
        if (auto* m = std::getenv ("PSYSNAP_MODTAB"))
            showModPage (*editor, juce::jlimit (0, 6, juce::String (m).getIntValue()));
        proc.uiDragSource = std::getenv ("PSYSNAP_DRAG") != nullptr ? psy::ModSrc::Lfo1 : psy::ModSrc::None;
        if (auto* d = std::getenv ("PSYSNAP_CHAINDRAG"))
            if (auto* chain = findChild<FxPage::Chain> (*editor, [] (FxPage::Chain&) { return true; }))
            {
                auto src = juce::Desktop::getInstance().getMainMouseSource();
                const auto at = [&] (juce::Point<float> pos, juce::Point<float> down, bool dragged) {
                    return juce::MouseEvent (src, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 0.f, 0.f, 0.f, 0.f, 0.f,
                                             chain, chain, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
                };
                const juce::Point<float> row1 { 60.f, 48.f + 15.f };
                const auto to = row1.translated (0.f, 30.f * (float) (juce::String (d).getIntValue() - 1) + 6.f);
                chain->mouseDown (at (row1, row1, false));
                chain->mouseDrag (at (row1.translated (0.f, 10.f), row1, true));
                chain->mouseDrag (at (to, row1, true));
            }
        if (auto* u = std::getenv ("PSYSNAP_UNISON"))
            if (auto* box = findChild<NumberBox> (*editor, [] (NumberBox& b) { return b.getParam() == psy::P::OscAUnison; }))
            {
                if (*u == '1' && box->onGear) box->onGear();
                if (*u == '2' && box->onActive) box->onActive (true);
            }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        if (auto* st = std::getenv ("PSYSNAP_STRIP"))
            if (auto* fx = findChild<FxPage> (*editor, [&] (FxPage& f) {
                    for (juce::Component* c = &f; c != editor.get(); c = c->getParentComponent())
                        if (! c->isVisible()) return false;
                    return true;
                }))
            {
                fx->showStrip (juce::String (st).getIntValue());
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            }
        if (std::getenv ("PSYSNAP_DNDTEST") != nullptr)
            return runDragAndDropTest (proc, *editor);

        const auto* scaleText = std::getenv ("PSYSNAP_SCALE");
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true,
                                                          scaleText != nullptr ? juce::String (scaleText).getFloatValue() : 2.f);
        const auto file = outDir.getChildFile ("ui_" + juce::String (c) + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("%s\n", file.getFullPathName().toRawUTF8());
    }
    editor.reset();
    return 0;
}
