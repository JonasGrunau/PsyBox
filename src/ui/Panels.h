#pragma once

#include "Displays.h"
#include "Widgets.h"

#include <memory>
#include <vector>

// Cells of the control grid (see theme::kCellW). x0 is the left edge of the
// first cell, which sits kCellInset left of the first knob.
struct CellGrid
{
    int x0 = 0, y0 = 0;
    juce::Rectangle<int> cell (int col, int row, int span = 1) const
    {
        return { x0 + col * theme::kCellW, y0 + row * (theme::kCellH + theme::kRowGap), span * theme::kCellW, theme::kCellH };
    }
    // A selector spanning cells, aligned to the knob edges, centred in the control area.
    juce::Rectangle<int> control (int col, int row, int span = 1) const
    {
        return cell (col, row, span).reduced (theme::kCellInset, 0).removeFromTop (theme::kKnobD)
            .withSizeKeepingCentre (theme::cellsW (span), theme::kCtrlH);
    }
    // Same span, full cell height, for components that draw their own caption.
    juce::Rectangle<int> captioned (int col, int row, int span = 1) const
    {
        return cell (col, row, span).reduced (theme::kCellInset, 0);
    }
};

// A titled area. Header row on top, the body below fills the rest (two rows
// of cells in a standard kRowH section).
class Section : public juce::Component
{
public:
    explicit Section (juce::String heading) : title (std::move (heading)) {}
    void paint (juce::Graphics&) override;

protected:
    juce::String title;
    juce::Rectangle<int> header() const { return { theme::kPad, theme::kPad, getWidth() - 2 * theme::kPad, theme::kHeaderH }; }
    juce::Rectangle<int> body() const
    {
        const int top = theme::kPad + theme::kHeaderH + theme::kHeaderGap;
        return { theme::kPad, top, getWidth() - 2 * theme::kPad, getHeight() - top - theme::kPad };
    }
    int titleWidth() const;   // title plus the gap after it
    // Grid whose first knob starts at x (defaults to the body's left edge).
    CellGrid grid (int x = -1) const { return { (x < 0 ? body().getX() : x) - theme::kCellInset, body().getY() }; }
    CellGrid gridAt (int x, int y) const { return { x - theme::kCellInset, y }; }
    static void place (juce::Component* c, juce::Rectangle<int> r) { c->setBounds (r); }

    // Captions drawn under selectors that have none of their own.
    struct Caption { juce::String text; juce::Rectangle<int> area; };
    std::vector<Caption> captions;
    void addCaption (const juce::String& t, juce::Rectangle<int> cell) { captions.push_back ({ t, cell.removeFromBottom (theme::kLabelH) }); }
};

// Helper that owns knobs for a panel.
struct KnobSet
{
    std::vector<std::unique_ptr<ParamKnob>> knobs;
    ParamKnob* add (juce::Component& parent, PsyBoxProcessor& p, psy::P prm, const juce::String& label = {})
    {
        knobs.push_back (std::make_unique<ParamKnob> (p, prm, label));
        parent.addAndMakeVisible (*knobs.back());
        return knobs.back().get();
    }
};

// ---------------------------------------------------------------------------
// "Playback & export", between the category list and the generator. Key:
// off, or one note every key plays (the arp starts from it) and both
// exports render. Batch: Generate renders that many new sounds of the
// category to WAVs on worker threads (the tile shows the progress), then
// the tile drags all of them out at once, one clip each.
// ---------------------------------------------------------------------------
class ExportPanel : public juce::Component,
                    public juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit ExportPanel (PsyBoxProcessor&);
    ~ExportPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    // divider, heading, key row, batch row, tile
    static constexpr int kTileH = 26;
    static constexpr int kHeight = theme::sp::md + theme::kLabelH + theme::sp::sm + theme::kCtrlH + theme::sp::sm
                                 + theme::kCtrlH + theme::sp::sm + kTileH + theme::sp::md;

private:
    // How many sounds: drag or scroll the number, double-click it to type one.
    class CountField : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit CountField (PsyBoxProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragging = false; repaint(); }
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { wheelAccum = 0.f; repaint(); }
        void refresh();

    private:
        PsyBoxProcessor& proc;
        juce::Label number;   // edited in place on a double-click
        int dragStart = 0;
        float wheelAccum = 0.f;
        bool dragging = false;
        void set (int n);
    };

    // The batch: empty, its progress, then the drag handle for every file.
    class BatchTile : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit BatchTile (PsyBoxProcessor&);
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void update (const PsyBoxProcessor::BatchStatus&);

    private:
        PsyBoxProcessor& proc;
        PsyBoxProcessor::BatchStatus status;
        bool dragging = false;
        bool ready() const { return ! status.running && status.finished > 0; }
    };

    PsyBoxProcessor& proc;
    juce::ComboBox keyBox, octaveBox;
    CountField count;
    juce::TextButton batchButton { "Generate" };
    BatchTile tile;
    juce::Rectangle<int> headingArea, keyCaption;
    void refresh();
    void timerCallback() override;   // the batch's progress
};

// ---------------------------------------------------------------------------
// Left column: the category list, then the generator controls. The list
// holds every tab as a header; the open tab's sounds sit under its header.
// ---------------------------------------------------------------------------
class Sidebar : public juce::Component, public juce::ChangeListener
{
public:
    explicit Sidebar (PsyBoxProcessor&);
    ~Sidebar() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    PsyBoxProcessor& proc;
    int hover = -1;                                        // index into entries
    int scrollY = 0, listH = 0;                            // the list scrolls inside listH
    psy::Category shownCategory = psy::Category::Count;   // last category scrolled into view
    juce::TextButton generateButton { "Generate" }, mutateButton { "Mutate" }, clearLocks { "Unlock" };
    ToggleDot arpToggle { juce::String() };   // categories with an arp; the caption is painted like SEED
    ToggleDot keyToggle { juce::String() };   // kick categories only: new kicks follow the played key
    ParamCombo arpKey, arpScale;              // the key and scale arps are written and played in
    ExportPanel exportPanel;                  // between the list and the info block
    // The key and scale only do something while the patch's arp plays them
    // or Generate writes arps (the ARP toggle): off otherwise. The scale also
    // dims while the key is off.
    std::unique_ptr<juce::ParameterAttachment> keyWatch, arpOnWatch;
    void updateArpKey();
    juce::Slider mutateAmount;
    juce::Label seedLabel;
    juce::Rectangle<int> seedCaption, arpCaption, keyCaption, arpKeyCaption, scaleCaption;

    // Which tab is open. Follows the category when it changes; opening a
    // tab selects its first category.
    psy::CategoryGroup openGroup = psy::CategoryGroup::Main;
    psy::Category lastCategory = psy::Category::Count;
    void showGroup (psy::CategoryGroup);

    // The list's rows, top to bottom: a header per tab, and the open tab's
    // sounds under its header. y is relative to the top of the list.
    struct Entry
    {
        psy::CategoryGroup group;
        psy::Category category;   // Category::Count for a header
        int y, h;
        bool isHeader() const { return category == psy::Category::Count; }
    };
    std::vector<Entry> entries;
    void layoutEntries();

    static constexpr int kHeaderH = 30, kItemH = 25;
    static constexpr int kListTop = theme::sp::xs;
    static constexpr int kInfoH = theme::kPad + 24 + theme::sp::xs + 30;   // category name + blurb
    int entryAt (juce::Point<int>) const;
    int contentH() const;
    juce::Rectangle<int> entryBounds (const Entry&) const;
    juce::Rectangle<int> listArea() const;
    juce::Rectangle<int> infoArea() const;
    int maxScroll() const;
    void setScroll (int y);
    void scrollToShow (psy::Category);
    void refresh();
};

// The pages of the editor, picked in the top bar like Serum's view tabs,
// in the order they are shown (Global always last).
enum class View : int { Main, Fx, Matrix, Arp, Global, Count };

// View tabs: text tabs with a neutral underline on the active one.
class ViewTabs : public juce::Component
{
public:
    void setViews (std::vector<View> views, const juce::String& mainName);
    void setSelected (View v) { if (v != selected) { selected = v; repaint(); } }
    View getSelected() const noexcept { return selected; }
    int idealWidth() const;
    // The widest the tabs ever get (every view, the longest main name): the
    // top bar keeps that much room, so nothing right of them moves when the
    // category changes the tabs.
    static int widestWidth();
    std::function<void (View)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    std::vector<View> views;
    juce::String mainName { "Osc" };
    View selected = View::Main;
    int hover = -1;
    juce::String nameOf (View) const;
    std::vector<juce::Rectangle<int>> bounds() const;
};

class TopBar : public juce::Component, public juce::ChangeListener
{
public:
    explicit TopBar (PsyBoxProcessor&);
    ~TopBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    ViewTabs views;

private:
    PsyBoxProcessor& proc;
    IconButton undoButton { IconButton::Icon::Undo, "Undo" };
    IconButton redoButton { IconButton::Icon::Redo, "Redo" };
    IconButton prev { IconButton::Icon::Left, "Previous preset" };
    IconButton next { IconButton::Icon::Right, "Next preset" };
    IconButton menu { IconButton::Icon::Menu, "Presets" };
    IconButton save { IconButton::Icon::Save, "Save preset" };
    ParamKnob level;    // the patch's own level, set by Generate so patches play equally loud
    ParamKnob volume;   // yours, kept through Generate
    Scope output;   // scope + level meter; drag it out to export a WAV
    juce::Rectangle<int> presetArea, levelCaption, volumeCaption;
    static juce::Font wordmarkFont();
    void showPresetMenu();
    void savePresetDialog();
    void saveNamed (const juce::String& name);   // confirms an overwrite, then writePreset
    void writePreset (const juce::String& name);
    std::unique_ptr<juce::AlertWindow> saveWindow;
};
