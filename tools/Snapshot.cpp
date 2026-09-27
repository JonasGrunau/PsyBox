// Renders the editor offscreen to PNGs, one per requested category, so the UI
// can be reviewed without a DAW or screen-recording permission.
//   psysynth_snapshot <out-dir> [category-index ...]
// Setting PSYSNAP_TAB=n also switches every tab strip to tab n (clamped),
// and PSYSNAP_LOCKS=1 locks a couple of parameters, to review those states.
// PSYSNAP_ARP=1 generates with an arp; PSYSNAP_PATTERN=1 opens the pattern editor.

#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

static void selectTabs (juce::Component& c, int tab)
{
    for (auto* child : c.getChildren())
    {
        if (auto* t = dynamic_cast<TabStrip*> (child))
        {
            t->setSelected (tab);
            if (t->onChange) t->onChange (t->getSelected());
        }
        selectTabs (*child, tab);
    }
}

static void openPatternEditor (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (auto* e = dynamic_cast<ArpPatternEditor*> (child))
            e->open();
        openPatternEditor (*child);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    std::vector<int> cats;
    for (int i = 2; i < argc; ++i) cats.push_back (juce::String (argv[i]).getIntValue());
    if (cats.empty()) cats = { 0 };

    PsySynthEditor::forceKeyboard = std::getenv ("PSYSNAP_KEYS") != nullptr;
    PsySynthProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

    proc.setArpGeneration (std::getenv ("PSYSNAP_ARP") != nullptr);
    for (int c : cats)
    {
        proc.setCategory ((psy::Category) juce::jlimit (0, psy::kNumCategories - 1, c));
        proc.generateWithSeed (0x1000u + (uint32_t) c * 7919u);

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
        if (std::getenv ("PSYSNAP_PATTERN") != nullptr)
            openPatternEditor (*editor);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);

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
