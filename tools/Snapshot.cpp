// Renders the editor offscreen to PNGs, one per requested category, so the UI
// can be reviewed without a DAW or screen-recording permission.
//   psybox_snapshot <out-dir> [category-index ...]
// Setting PSYSNAP_TAB=n also switches every tab strip to tab n (clamped),
// and PSYSNAP_LOCKS=1 locks a couple of parameters, to review those states.
// PSYSNAP_VIEW=n opens view n (0 Osc/Kick, 1 FX, 2 Matrix, 3 Arp, 4 Global).
// PSYSNAP_MODTAB=n opens page n of the modulators (0 Amp Env ... 6 LFO 3).
// PSYSNAP_DRAG=1 shows the knobs as they look while a mod source is dragged.
// PSYSNAP_DROP="7:oscA_level,15:delay_mix" drops mod source 7 (LFO 2) and 15
// (Macro 2) on those knobs, through the knobs' own drop handling.
// PSYSNAP_DNDTEST=1 opens the editor in a window and drives the mouse through
// JUCE (no OS events): drags LFO 2 onto Osc A's level, then the new ring
// upwards, and prints PASS/FAIL for each step.
// PSYSNAP_ARP=1 generates with an arp.
// PSYSNAP_PARAMS="comp_on=1,comp_mode=0" sets plain parameter values after Generate.

#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

#include <typeinfo>

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
    auto* fine = findChild<ParamKnob> (editor, [] (ParamKnob& k) { return k.getParam() == psy::P::OscAFine; });
    if (handle == nullptr || knob == nullptr || fine == nullptr) { std::printf ("FAIL controls not found\n"); return 1; }

    // drop
    const juce::DragAndDropTarget::SourceDetails d (moddrag::describe (handle->getSource()), handle, {});
    check (knob->isInterestedInDragSource (d), "Osc A level takes a dragged LFO");
    check (! fine->isInterestedInDragSource (d), "Osc A fine (no mod target) refuses it");
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
    std::printf ("macro names: %s / %s / %s / %s\n", proc.getMacroName (0).toRawUTF8(), proc.getMacroName (1).toRawUTF8(),
                 proc.getMacroName (2).toRawUTF8(), proc.getMacroName (3).toRawUTF8());
    return failures;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    std::vector<int> cats;
    for (int i = 2; i < argc; ++i) cats.push_back (juce::String (argv[i]).getIntValue());
    if (cats.empty()) cats = { 0 };

    PsyBoxEditor::forceKeyboard = std::getenv ("PSYSNAP_KEYS") != nullptr;
    PsyBoxProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

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
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        if (std::getenv ("PSYSNAP_DNDTEST") != nullptr)
            return runDragAndDropTest (proc, *editor);

        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.f);
        const auto file = outDir.getChildFile ("ui_" + juce::String (c) + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("%s\n", file.getFullPathName().toRawUTF8());
    }
    editor.reset();
    return 0;
}
