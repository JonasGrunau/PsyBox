// Renders the editor offscreen to PNGs, one per requested category, so the UI
// can be reviewed without a DAW or screen-recording permission.
//   psysynth_snapshot <out-dir> [category-index ...]

#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    std::vector<int> cats;
    for (int i = 2; i < argc; ++i) cats.push_back (juce::String (argv[i]).getIntValue());
    if (cats.empty()) cats = { 0 };

    PsySynthProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setSize (PsySynthEditor::kW, PsySynthEditor::kH);

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
