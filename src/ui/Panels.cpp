#include "Panels.h"

using namespace theme;
using psy::P;

namespace
{
    juce::String hexSeed (uint32_t s) { return juce::String::toHexString ((int) s).toUpperCase().paddedLeft ('0', 6); }
} // namespace

// ---------------------------------------------------------------------------
// Section
// ---------------------------------------------------------------------------
void Section::paint (juce::Graphics& g)
{
    drawHeading (g, title, header());
    g.setColour (col::textDim);
    g.setFont (label (10.f));
    for (const auto& c : captions)
        g.drawText (c.text.toUpperCase(), c.area, juce::Justification::centred, false);
}

int Section::titleWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt (heading(), title.toUpperCase()) + sp::md;
}

// ---------------------------------------------------------------------------
// ExportPanel
// ---------------------------------------------------------------------------
ExportPanel::ExportPanel (PsyBoxProcessor& p) : proc (p), count (p), tile (p)
{
    static constexpr const char* keys[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    keyBox.addItem ("Off", 1);
    for (int k = 0; k < 12; ++k)
        keyBox.addItem (keys[k], k + 2);
    keyBox.setTooltip ("Playback key: every key you press plays this one note (the arp starts from it), and the WAV export "
                       "and the batch render it. Off: the keys you play");
    keyBox.onChange = [this] { proc.setPlayKey (keyBox.getSelectedId() - 2, proc.getPlayOctave()); };
    for (int o = PsyBoxProcessor::kMinPlayOctave; o <= PsyBoxProcessor::kMaxPlayOctave; ++o)
        octaveBox.addItem (juce::String (o), o + 1);
    octaveBox.setTooltip ("The playback key's octave (middle C is C4)");
    octaveBox.onChange = [this] { proc.setPlayKey (proc.getPlayKey(), octaveBox.getSelectedId() - 1); };
    batchButton.setTooltip ("Render that many new sounds of this category as WAVs, then drag them all out below. "
                            "The sound playing stays");
    batchButton.onClick = [this] {
        if (proc.getBatchStatus().running) proc.cancelBatch();
        else proc.startBatch (proc.getBatchCount());
        refresh();
    };
    for (juce::Component* c : { (juce::Component*) &keyBox, (juce::Component*) &octaveBox, (juce::Component*) &count,
                                (juce::Component*) &batchButton, (juce::Component*) &tile })
        addAndMakeVisible (c);
    proc.addChangeListener (this);
    refresh();
}

ExportPanel::~ExportPanel() { proc.removeChangeListener (this); }

void ExportPanel::refresh()
{
    keyBox.setSelectedId (proc.getPlayKey() + 2, juce::dontSendNotification);
    octaveBox.setSelectedId (proc.getPlayOctave() + 1, juce::dontSendNotification);
    const bool keyed = proc.getPlayKey() >= 0;
    octaveBox.setEnabled (keyed);
    octaveBox.setAlpha (keyed ? 1.f : 0.45f);
    count.refresh();
    timerCallback();
}

void ExportPanel::timerCallback()
{
    const auto st = proc.getBatchStatus();
    batchButton.setButtonText (st.running ? "Cancel" : "Generate");
    tile.update (st);
    if (st.running && ! isTimerRunning()) startTimerHz (15);
    else if (! st.running && isTimerRunning()) stopTimer();
}

void ExportPanel::paint (juce::Graphics& g)
{
    g.setColour (col::line);
    g.fillRect (0, 0, getWidth() - 1, 1);
    drawHeading (g, "Playback & export", headingArea);
    g.setColour (col::textFaint);
    g.setFont (label (10.f));
    g.drawText ("KEY", keyCaption, juce::Justification::centredLeft, false);
}

void ExportPanel::resized()
{
    auto a = getLocalBounds().reduced (kPad, 0).withTrimmedTop (sp::md);
    headingArea = a.removeFromTop (kLabelH);
    a.removeFromTop (sp::sm);
    // caption left, the note and its octave in the column the arp's key uses
    auto keyRow = a.removeFromTop (kCtrlH);
    keyCaption = keyRow;
    octaveBox.setBounds (keyRow.removeFromRight (44));
    keyRow.removeFromRight (sp::xs);
    keyBox.setBounds (keyRow.removeFromRight (64));
    a.removeFromTop (sp::sm);
    // two halves, like Mutate and its amount
    auto batchRow = a.removeFromTop (kCtrlH);
    batchButton.setBounds (batchRow.removeFromLeft ((batchRow.getWidth() - sp::md) / 2));
    batchRow.removeFromLeft (sp::md);
    count.setBounds (batchRow);
    a.removeFromTop (sp::sm);
    tile.setBounds (a.removeFromTop (kTileH));
}

ExportPanel::CountField::CountField (PsyBoxProcessor& p) : proc (p)
{
    number.setFont (mono (11.5f));
    number.setJustificationType (juce::Justification::centredRight);
    number.setBorderSize ({ 0, 0, 0, 0 });
    number.setColour (juce::Label::textColourId, col::text);
    number.setEditable (false, true, false);
    number.setInterceptsMouseClicks (false, true);   // the field drags; its editor takes clicks
    number.onEditorShow = [this] {
        if (auto* ed = number.getCurrentTextEditor())
        {
            ed->setInputRestrictions (3, "0123456789");
            ed->setJustification (juce::Justification::centredRight);
        }
    };
    number.onTextChange = [this] {
        const auto t = number.getText().trim();
        set (t.isNotEmpty() ? t.getIntValue() : proc.getBatchCount());
    };
    addAndMakeVisible (number);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setTooltip ("How many sounds the batch renders (1 to " + juce::String (PsyBoxProcessor::kMaxBatch)
                + "): drag or scroll, double-click to type");
    refresh();
}

void ExportPanel::CountField::refresh()
{
    number.setText (juce::String (proc.getBatchCount()), juce::dontSendNotification);
}

void ExportPanel::CountField::set (int n)
{
    proc.setBatchCount (n);
    refresh();
}

void ExportPanel::CountField::resized()
{
    number.setBounds (getLocalBounds().reduced (sp::sm - 2, 0).removeFromRight (36));
}

void ExportPanel::CountField::paint (juce::Graphics& g)
{
    // the inline number field (like SEM and FIN): caption left, value right
    const auto f = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (f, 3.f);
    g.setColour (isMouseOver() || dragging ? col::textFaint : col::lineStrong);
    g.drawRoundedRectangle (f.reduced (0.5f), 3.f, 1.f);
    g.setColour (col::textDim);
    g.setFont (label (10.f));
    g.drawText ("SOUNDS", getLocalBounds().reduced (sp::sm - 2, 0), juce::Justification::centredLeft, false);
}

void ExportPanel::CountField::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    dragStart = proc.getBatchCount();
    repaint();
}

void ExportPanel::CountField::mouseDrag (const juce::MouseEvent& e)
{
    const float px = e.mods.isShiftDown() ? 12.f : 3.f;   // a step per 3 px, Shift for fine steps
    set (dragStart + (int) std::trunc ((float) -e.getDistanceFromDragStartY() / px));
}

void ExportPanel::CountField::mouseDoubleClick (const juce::MouseEvent&)
{
    number.showEditor();
}

void ExportPanel::CountField::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    // a trackpad flick sends dozens of tiny events: collect them and step once per notch
    if (w.isInertial || w.deltaY == 0.f) return;
    constexpr float notch = 0.12f;
    if ((wheelAccum > 0.f) != (w.deltaY > 0.f)) wheelAccum = 0.f;
    wheelAccum += w.deltaY;
    if (std::abs (wheelAccum) < notch) return;
    const float dir = wheelAccum > 0.f ? 1.f : -1.f;
    wheelAccum = juce::jlimit (-notch, notch, wheelAccum - dir * notch);
    set (proc.getBatchCount() + (int) dir);
}

ExportPanel::BatchTile::BatchTile (PsyBoxProcessor& p) : proc (p)
{
    setTooltip ("The batch. Drag it out to drop every WAV into your DAW, one clip each. Double-click to open its folder");
}

void ExportPanel::BatchTile::update (const PsyBoxProcessor::BatchStatus& st)
{
    const bool same = st.total == status.total && st.finished == status.finished && st.failed == status.failed
                      && st.running == status.running && std::abs (st.progress - status.progress) < 0.002f;
    status = st;
    setMouseCursor (ready() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    if (! same) repaint();
}

void ExportPanel::BatchTile::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    juce::Path shape;
    shape.addRoundedRectangle (r, 3.f);
    g.setColour (col::well);
    g.fillPath (shape);

    juce::String text;
    auto textColour = col::textFaint;
    if (status.running)
    {
        // the progress fills the tile from the left
        g.saveState();
        g.reduceClipRegion (shape);
        g.setColour (accent (*this).withAlpha (0.22f));
        g.fillRect (r.withWidth (r.getWidth() * status.progress));
        g.restoreState();
        text = "RENDERING " + juce::String (status.finished + status.failed) + " / " + juce::String (status.total);
        textColour = col::text;
    }
    else if (ready())
    {
        text = dragging ? juce::String ("DROP IN YOUR DAW")
                        : "DRAG " + juce::String (status.finished) + (status.finished == 1 ? " WAV" : " WAVS")
                              + (status.failed > 0 ? ", " + juce::String (status.failed) + " FAILED" : juce::String());
        textColour = accent (*this);
    }
    else
    {
        text = status.total > 0 ? "BATCH FAILED" : "NO BATCH YET";
    }
    g.setColour (ready() ? accent (*this).withAlpha (isMouseOver() || dragging ? 1.f : 0.55f) : col::lineStrong);
    g.strokePath (shape, juce::PathStrokeType (1.f));
    g.setColour (textColour);
    g.setFont (heading (9.5f));
    g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
}

void ExportPanel::BatchTile::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! ready() || e.getDistanceFromDragStart() < 6)
        return;
    const auto files = proc.getBatchFiles();
    if (files.isEmpty())
        return;
    dragging = true;
    repaint();
    // every file in one drag: a DAW lays them out one after another, a clip each
    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this,
                                                                 [safe = juce::Component::SafePointer<BatchTile> (this)] {
                                                                     if (safe == nullptr) return;
                                                                     safe->dragging = false;
                                                                     safe->repaint();
                                                                 });
}

void ExportPanel::BatchTile::mouseDoubleClick (const juce::MouseEvent&)
{
    if (const auto folder = proc.getBatchFolder(); folder.isDirectory())
        folder.startAsProcess();
}

// ---------------------------------------------------------------------------
// Sidebar
// ---------------------------------------------------------------------------
Sidebar::Sidebar (PsyBoxProcessor& p) : proc (p), arpKey (p, psy::P::ArpKey), arpScale (p, psy::P::ArpScale), exportPanel (p)
{
    addAndMakeVisible (exportPanel);
    generateButton.getProperties().set ("primary", true);
    generateButton.setTooltip ("New sound in this category (locked knobs are kept)");
    generateButton.onClick = [this] { proc.generate(); };
    mutateButton.setTooltip ("Vary the current sound within the category");
    mutateButton.onClick = [this] { proc.mutate(); };
    clearLocks.onClick = [this] { proc.clearLocks(); };
    arpToggle.setTooltip ("Generate adds an arp in the style of the category");
    arpToggle.setStacked (true);
    addChildComponent (arpToggle);
    arpToggle.onToggle = [this] (bool on) { proc.setArpGeneration (on); };
    keyToggle.setTooltip ("Generate tunes the kick's body to the key you play (key tracking)");
    keyToggle.setStacked (true);
    keyToggle.onToggle = [this] (bool on) { proc.setKickKeyGeneration (on); };
    addChildComponent (keyToggle);
    arpKey.setTooltip ("The arp's key: generated lines are written in it, and the arp plays every note in its scale. Off: any key. "
                       "Needs an arp: ARP on, or the arp on in the ARP view");
    arpScale.setTooltip ("The scale the arp keeps to when a key is set. Needs an arp: ARP on, or the arp on in the ARP view");
    addChildComponent (arpKey);
    addChildComponent (arpScale);
    keyWatch = std::make_unique<juce::ParameterAttachment> (*p.param (psy::P::ArpKey), [this] (float) { updateArpKey(); });
    arpOnWatch = std::make_unique<juce::ParameterAttachment> (*p.param (psy::P::ArpOn), [this] (float) { updateArpKey(); });

    mutateAmount.setSliderStyle (juce::Slider::LinearHorizontal);
    mutateAmount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mutateAmount.setRange (0.02, 1.0);
    mutateAmount.setValue (proc.getMutateAmount(), juce::dontSendNotification);
    mutateAmount.setTooltip ("Mutation amount");
    mutateAmount.onValueChange = [this] { proc.setMutateAmount ((float) mutateAmount.getValue()); repaint (infoArea()); };

    seedLabel.setFont (mono (10.f));
    seedLabel.setBorderSize ({ 0, 0, 0, 0 });
    seedLabel.setColour (juce::Label::textColourId, col::textFaint);
    seedLabel.setJustificationType (juce::Justification::centredRight);
    seedLabel.setEditable (false, true);
    seedLabel.setTooltip ("The seed of the current sound. Same category + same seed = same sound. Double-click to type one.");
    seedLabel.onTextChange = [this] {
        const auto t = seedLabel.getText().trim().removeCharacters ("#");
        if (t.containsOnly ("0123456789abcdefABCDEF") && t.isNotEmpty())
            proc.generateWithSeed ((uint32_t) t.getHexValue32() & 0xFFFFFFu);
        refresh();
    };

    for (juce::Component* c : { (juce::Component*) &generateButton, (juce::Component*) &mutateButton, (juce::Component*) &clearLocks,
                                (juce::Component*) &mutateAmount, (juce::Component*) &seedLabel })
        addAndMakeVisible (c);
    proc.addChangeListener (this);
    layoutEntries();
    refresh();
}

Sidebar::~Sidebar() { proc.removeChangeListener (this); }

void Sidebar::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }

void Sidebar::updateArpKey()
{
    const bool live = proc.getArpGeneration() || proc.plain (psy::P::ArpOn) > 0.5f;
    arpKey.setEnabled (live);
    arpScale.setEnabled (live);
    arpKey.setAlpha (live ? 1.f : 0.45f);
    arpScale.setAlpha (live && proc.plain (psy::P::ArpKey) > 0.5f ? 1.f : 0.45f);
    repaint (arpKeyCaption);
    repaint (scaleCaption);
}

void Sidebar::refresh()
{
    seedLabel.setText (hexSeed (proc.getSeed()), juce::dontSendNotification);
    arpToggle.setOn (proc.getArpGeneration());
    updateArpKey();
    keyToggle.setOn (proc.getKickKeyGeneration());
    // only the generator options that apply to the current category
    const bool arp = proc.categoryHasArp();
    if (keyToggle.isVisible() != proc.isKickCategory() || arpToggle.isVisible() != arp)
    {
        keyToggle.setVisible (proc.isKickCategory());
        arpToggle.setVisible (arp);
        arpKey.setVisible (arp);
        arpScale.setVisible (arp);
        resized();
    }
    if (proc.getCategory() != lastCategory)
    {
        lastCategory = proc.getCategory();
        showGroup (psy::groupOf (lastCategory));
    }
    clearLocks.setVisible (proc.numLocks() > 0);
    clearLocks.setTooltip ("Unlock all " + juce::String (proc.numLocks()) + " locked parameters");
    if (proc.getCategory() != shownCategory && listH > 0)
        scrollToShow (proc.getCategory());   // e.g. a session that loads with a hidden category
    repaint();
}

void Sidebar::layoutEntries()
{
    entries.clear();
    int y = 0;
    for (int t = 0; t < psy::kNumCategoryGroups; ++t)
    {
        const auto g = (psy::CategoryGroup) t;
        entries.push_back ({ g, psy::Category::Count, y, kHeaderH });
        y += kHeaderH;
        if (g != openGroup) continue;
        for (psy::Category c : psy::categoriesIn (g))
        {
            entries.push_back ({ g, c, y, kItemH });
            y += kItemH;
        }
        y += sp::sm;   // room between the open tab's last sound and the next header
    }
}

int Sidebar::contentH() const
{
    return entries.empty() ? 0 : entries.back().y + entries.back().h;
}

juce::Rectangle<int> Sidebar::entryBounds (const Entry& e) const
{
    return { 0, kListTop + e.y - scrollY, getWidth() - 1, e.h };
}

// The visible window of the list; entries scroll inside it.
juce::Rectangle<int> Sidebar::listArea() const
{
    return { 0, kListTop, getWidth() - 1, listH };
}

// Everything below the list and the export panel, starting at the divider.
juce::Rectangle<int> Sidebar::infoArea() const
{
    return getLocalBounds().withTrimmedTop (kListTop + listH + ExportPanel::kHeight);
}

int Sidebar::maxScroll() const
{
    return juce::jmax (0, contentH() - listH);
}

void Sidebar::showGroup (psy::CategoryGroup g)
{
    if (g == openGroup && shownCategory != psy::Category::Count) return;
    openGroup = g;
    layoutEntries();
    scrollY = 0;
    hover = -1;
    shownCategory = psy::Category::Count;
    if (listH > 0) scrollToShow (proc.getCategory());
    repaint();
}

void Sidebar::setScroll (int y)
{
    y = juce::jlimit (0, maxScroll(), y);
    if (y != scrollY) { scrollY = y; repaint(); }
}

void Sidebar::scrollToShow (psy::Category c)
{
    shownCategory = c;
    const auto it = std::find_if (entries.begin(), entries.end(), [c] (const Entry& e) { return e.category == c; });
    if (it == entries.end()) return;   // its tab is closed
    if (it->y < scrollY) setScroll (it->y);
    else if (it->y + it->h > scrollY + listH) setScroll (it->y + it->h - listH);
}

// Returns the index of the entry under the point, or -1.
int Sidebar::entryAt (juce::Point<int> pt) const
{
    if (! listArea().contains (pt)) return -1;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entryBounds (entries[(size_t) i]).contains (pt)) return i;
    return -1;
}

void Sidebar::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());

    const int sel = (int) proc.getCategory();
    const auto& info = psy::categoryInfo();
    const auto colourOf = [&] (psy::Category c) { return juce::Colour ((juce::uint32) (0xff000000 | info[(size_t) c].colour)); };
    const auto list = listArea();
    g.saveState();   // clip the entries (and their scroll hints) to the list window
    g.reduceClipRegion (list);
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        const auto r = entryBounds (e);
        if (! r.intersects (list)) continue;
        const bool hot = i == hover;

        if (e.isHeader())
        {
            // a hairline between tabs, the name, and a dot per sound while closed
            const bool open = e.group == openGroup;
            if (i > 0)
            {
                g.setColour (col::line);
                g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);
            }
            if (hot && ! open)
            {
                g.setColour (juce::Colours::white.withAlpha (0.03f));
                g.fillRect (r.withTrimmedTop (1));
            }
            auto t = r.reduced (kPad, 0);
            if (! open)
            {
                const auto& sounds = psy::categoriesIn (e.group);
                constexpr float kDot = 5.f, kStep = 8.f;
                float x = (float) t.getRight() - kDot;
                for (auto it = sounds.rbegin(); it != sounds.rend(); ++it, x -= kStep)
                {
                    g.setColour (colourOf (*it).withAlpha (hot ? 1.f : 0.75f));
                    g.fillEllipse (x, (float) r.getCentreY() - kDot / 2.f, kDot, kDot);
                }
            }
            g.setFont (heading (9.5f));
            g.setColour (open ? col::text : (hot ? col::text.withAlpha (0.8f) : col::textDim));
            g.drawText (juce::String (psy::categoryGroupName (e.group)).toUpperCase(), t, juce::Justification::centredLeft, false);
            continue;
        }

        const int c = (int) e.category;
        const auto colour = colourOf (e.category);
        const bool isSel = c == sel;
        if (isSel)
        {
            g.setColour (colour.withAlpha (0.10f));
            g.fillRect (r);
            g.setColour (colour);
            g.fillRect (r.getX(), r.getY(), 2, r.getHeight());
        }
        else if (hot)
        {
            g.setColour (juce::Colours::white.withAlpha (0.03f));
            g.fillRect (r);
        }
        const float cy = (float) r.getCentreY();
        g.setColour (isSel || hot ? colour : colour.withAlpha (0.85f));
        g.fillEllipse ((float) kPad, cy - 3.5f, 7.f, 7.f);
        g.setColour (isSel ? col::text : (hot ? col::text.withAlpha (0.85f) : col::textDim));
        g.setFont (isSel ? labelStrong (12.5f) : label (12.5f));
        g.drawText (info[(size_t) c].name, r.withTrimmedLeft (kPad + 7 + sp::md), juce::Justification::centredLeft, false);
    }

    // scroll hints: rows fade out at an edge with more beyond it, and a thin thumb
    if (maxScroll() > 0)
    {
        constexpr float kFade = 18.f;
        const auto top = (float) list.getY(), bottom = (float) list.getBottom();
        if (scrollY > 0)
        {
            g.setGradientFill ({ col::panel, 0.f, top, col::panel.withAlpha (0.f), 0.f, top + kFade, false });
            g.fillRect (list.withHeight ((int) kFade));
        }
        if (scrollY < maxScroll())
        {
            g.setGradientFill ({ col::panel, 0.f, bottom, col::panel.withAlpha (0.f), 0.f, bottom - kFade, false });
            g.fillRect (list.withTrimmedTop (list.getHeight() - (int) kFade));
        }
        const int thumbH = juce::jmax (24, listH * listH / juce::jmax (1, contentH()));
        const int thumbY = list.getY() + (listH - thumbH) * scrollY / maxScroll();
        g.setColour (col::textFaint.withAlpha (0.5f));
        g.fillRoundedRectangle (juce::Rectangle<int> (getWidth() - 5, thumbY, 2, thumbH).toFloat(), 1.f);
    }
    g.restoreState();

    // info block
    auto a = infoArea();
    g.setColour (col::line);
    g.fillRect (0, a.getY(), getWidth() - 1, 1);
    a = a.reduced (kPad, 0).withTrimmedTop (kPad);
    g.setColour (accent (*this));
    g.setFont (title (17.f));
    g.drawText (info[(size_t) sel].name, a.removeFromTop (24), juce::Justification::centredLeft, false);
    a.removeFromTop (sp::xs);
    g.setColour (col::textDim);
    g.setFont (label (11.f));
    g.drawFittedText (info[(size_t) sel].blurb, a.removeFromTop (30), juce::Justification::topLeft, 2, 1.f);

    // mutate amount caption and readout, in the label row above the slider
    const auto cap = mutateAmount.getBounds().withY (mutateButton.getY()).withHeight (kLabelH);
    g.setColour (col::textFaint);
    g.setFont (mono (10.f));
    g.drawText (juce::String (juce::roundToInt (proc.getMutateAmount() * 100.f)) + "%", cap, juce::Justification::centredRight, false);
    g.setFont (label (10.f));
    g.drawText ("AMOUNT", cap, juce::Justification::centredLeft, false);
    g.drawText ("SEED", seedCaption, juce::Justification::centredLeft, false);
    if (arpToggle.isVisible())
        g.drawText ("ARP", arpCaption, juce::Justification::centredLeft, false);
    if (arpKey.isVisible())
    {
        g.setColour (arpKey.getAlpha() < 1.f ? col::textFaint.withAlpha (0.6f) : col::textFaint);
        g.drawText ("KEY", arpKeyCaption, juce::Justification::centredLeft, false);
        g.setColour (arpScale.getAlpha() < 1.f ? col::textFaint.withAlpha (0.6f) : col::textFaint);
        g.drawText ("SCALE", scaleCaption, juce::Justification::centredLeft, false);
    }
    if (keyToggle.isVisible())
        g.drawText ("TUNE TO KEY", keyCaption, juce::Justification::centredLeft, false);

}

void Sidebar::resized()
{
    // Built from the bottom edge up so the buttons sit on the window's padding.
    auto a = getLocalBounds().reduced (kPad);
    auto r = a.removeFromBottom (32);   // mutate + amount
    mutateButton.setBounds (r.removeFromLeft ((r.getWidth() - sp::md) / 2));
    r.removeFromLeft (sp::md);
    mutateAmount.setBounds (r.withTrimmedTop (kLabelH));
    a.removeFromBottom (sp::sm);

    generateButton.setBounds (a.removeFromBottom (44));
    a.removeFromBottom (sp::md);

    // The options that shape the next Generate sit right above it.
    // "SEED" caption left, value right: same pattern as AMOUNT / 25% below
    auto seedRow = a.removeFromBottom (kLabelH);
    seedCaption = seedRow;
    seedLabel.setBounds (seedRow.removeFromRight (100));

    // "ARP", "KEY" and "SCALE" (categories that have an arp): whether
    // Generate adds one, and the key and scale it is written and played in.
    // Caption left, the choice right, like SEED.
    arpCaption = arpKeyCaption = scaleCaption = {};
    if (arpToggle.isVisible())
    {
        a.removeFromBottom (sp::sm);
        auto scaleRow = a.removeFromBottom (kCtrlH);
        scaleCaption = scaleRow;
        arpScale.setBounds (scaleRow.removeFromRight (112));
        a.removeFromBottom (sp::sm);
        auto keyRow = a.removeFromBottom (kCtrlH);
        arpKeyCaption = keyRow;
        arpKey.setBounds (keyRow.removeFromRight (112));
        a.removeFromBottom (sp::sm);
        auto arpRow = a.removeFromBottom (kLabelH);
        arpCaption = arpRow;
        arpToggle.setBounds (arpRow.removeFromRight (26));
    }

    // "TUNE TO KEY" (kick categories): whether Generate key-tracks the kick
    keyCaption = {};
    if (keyToggle.isVisible())
    {
        a.removeFromBottom (sp::sm);
        auto keyRow = a.removeFromBottom (kLabelH);
        keyCaption = keyRow;
        keyToggle.setBounds (keyRow.removeFromRight (26));
    }

    // The list runs down to the export panel above the info block; the rest scroll.
    listH = juce::jmax (kItemH, a.getBottom() - sp::sm - kInfoH - kListTop - ExportPanel::kHeight);
    exportPanel.setBounds (0, kListTop + listH, getWidth(), ExportPanel::kHeight);
    setScroll (scrollY);
    scrollToShow (proc.getCategory());

    // Unlock sits on the category name line, right-aligned.
    auto nameLine = infoArea().reduced (kPad, 0).withTrimmedTop (kPad).removeFromTop (24);
    clearLocks.setBounds (nameLine.removeFromRight (64).withSizeKeepingCentre (64, kCtrlH));
}

void Sidebar::mouseDown (const juce::MouseEvent& e)
{
    const int i = entryAt (e.getPosition());
    if (i < 0) return;
    const auto entry = entries[(size_t) i];
    if (! entry.isHeader())
    {
        proc.setCategory (entry.category);
        return;
    }
    // opening a tab selects its first sound (unless the current one is in it)
    if (psy::groupOf (proc.getCategory()) != entry.group)
        proc.setCategory (psy::categoriesIn (entry.group).front());
    showGroup (entry.group);
}

void Sidebar::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = entryAt (e.getPosition());
    if (i >= 0 && ! entries[(size_t) i].isHeader()) proc.generate();
}

void Sidebar::mouseMove (const juce::MouseEvent& e)
{
    int i = entryAt (e.getPosition());
    if (i >= 0 && entries[(size_t) i].isHeader() && entries[(size_t) i].group == openGroup)
        i = -1;   // the open tab's header does nothing
    if (i != hover) { hover = i; repaint(); }
    setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void Sidebar::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // deltaY already follows the OS scroll direction; isReversed only reports it (Viewport ignores it too)
    const float dy = w.deltaY;
    if (maxScroll() == 0 || dy == 0.f || ! listArea().contains (e.getPosition()))
    {
        Component::mouseWheelMove (e, w);
        return;
    }
    // same distance per notch as juce::Viewport with a 16 px step
    const float px = dy * 14.f * 16.f;
    setScroll (scrollY - juce::roundToInt (px < 0.f ? juce::jmin (px, -1.f) : juce::jmax (px, 1.f)));
    mouseMove (e);   // a different row is under the pointer now
}

// ---------------------------------------------------------------------------
// ViewTabs
// ---------------------------------------------------------------------------
void ViewTabs::setViews (std::vector<View> v, const juce::String& main)
{
    views = std::move (v);
    mainName = main;
    repaint();
}

juce::String ViewTabs::nameOf (View v) const
{
    switch (v)
    {
        case View::Main:   return mainName;
        case View::Fx:     return "FX";
        case View::Matrix: return "Matrix";
        case View::Arp:    return "Arp";
        case View::Global: return "Global";
        case View::Count:  break;
    }
    return {};
}

std::vector<juce::Rectangle<int>> ViewTabs::bounds() const
{
    std::vector<juce::Rectangle<int>> out;
    int x = 0;
    const auto f = heading (10.5f);
    for (auto v : views)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (f, nameOf (v).toUpperCase());
        out.push_back ({ x, 0, w, getHeight() });
        x += w + sp::xl;
    }
    return out;
}

int ViewTabs::idealWidth() const
{
    const auto b = bounds();
    return b.empty() ? 0 : b.back().getRight();
}

int ViewTabs::widestWidth()
{
    ViewTabs all;
    int widest = 0;
    for (const char* main : { "Osc", "Kick", "Phoscyon" })
    {
        all.setViews ({ View::Main, View::Fx, View::Matrix, View::Arp, View::Global }, main);
        widest = std::max (widest, all.idealWidth());
    }
    return widest;
}

void ViewTabs::paint (juce::Graphics& g)
{
    const auto b = bounds();
    g.setFont (heading (10.5f));
    for (size_t i = 0; i < views.size(); ++i)
    {
        const bool sel = views[i] == selected;
        g.setColour (sel ? col::text : ((int) i == hover ? col::text.withAlpha (0.8f) : col::textDim));
        g.drawText (nameOf (views[i]).toUpperCase(), b[i], juce::Justification::centred, false);
        if (sel)
        {
            // neutral, at the bottom edge of the bar, over the hairline
            g.setColour (col::text);
            g.fillRect (b[i].getX(), getHeight() - 2, b[i].getWidth(), 2);
        }
    }
}

void ViewTabs::mouseDown (const juce::MouseEvent& e)
{
    const auto b = bounds();
    for (size_t i = 0; i < views.size(); ++i)
        if (b[i].expanded (sp::sm, 0).contains (e.getPosition()))
        {
            setSelected (views[i]);
            if (onChange) onChange (views[i]);
        }
}

void ViewTabs::mouseMove (const juce::MouseEvent& e)
{
    const auto b = bounds();
    int h = -1;
    for (size_t i = 0; i < views.size(); ++i)
        if (b[i].expanded (sp::sm, 0).contains (e.getPosition())) h = (int) i;
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

// ---------------------------------------------------------------------------
// TopBar
// ---------------------------------------------------------------------------
TopBar::TopBar (PsyBoxProcessor& p)
    : proc (p), level (p, P::PatchGain, "Level"), volume (p, P::MasterVol, "Vol"), output (p)
{
    for (juce::Component* c : { (juce::Component*) &views, (juce::Component*) &undoButton, (juce::Component*) &redoButton,
                                (juce::Component*) &prev, (juce::Component*) &next, (juce::Component*) &menu,
                                (juce::Component*) &save, (juce::Component*) &level, (juce::Component*) &volume,
                                (juce::Component*) &output })
        addAndMakeVisible (c);
    prev.onClick = [this] { proc.stepPreset (-1); };
    next.onClick = [this] { proc.stepPreset (1); };
    menu.onClick = [this] { showPresetMenu(); };
    save.onClick = [this] { savePresetDialog(); };
    undoButton.onClick = [this] { proc.undo(); };
    redoButton.onClick = [this] { proc.redo(); };
    proc.addChangeListener (this);
    changeListenerCallback (nullptr);
}

TopBar::~TopBar() { proc.removeChangeListener (this); }

void TopBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());
    repaint();
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    // wordmark
    g.setColour (col::text);
    g.setFont (wordmarkFont());
    g.drawText ("PSYBOX", juce::Rectangle<int> (kPad, 0, 160, getHeight()), juce::Justification::centredLeft, false);

    // preset display
    auto r = presetArea;
    g.setColour (col::well);
    g.fillRoundedRectangle (r.toFloat(), 4.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.f, 1.f);
    auto text = r.withTrimmedLeft (prev.getRight() - r.getX() + sp::sm).withTrimmedRight (r.getRight() - next.getX() + sp::sm);
    const auto& info = psy::categoryInfo()[(size_t) proc.getCategory()];
    g.setColour (accent (*this));
    g.setFont (heading (9.5f));
    const auto catName = juce::String (info.shortName).toUpperCase();
    const int cw = juce::GlyphArrangement::getStringWidthInt (heading (9.5f), catName) + sp::md;
    g.drawText (catName, text.removeFromLeft (cw), juce::Justification::centredLeft, false);
    g.setColour (col::text);
    g.setFont (label (12.5f));
    g.drawText (proc.getPresetName(), text, juce::Justification::centredLeft, true);

    g.setColour (col::textFaint);
    g.setFont (label (10.f));
    g.drawText ("LEVEL", levelCaption, juce::Justification::centredRight, false);
    g.drawText ("VOL", volumeCaption, juce::Justification::centredRight, false);
}

void TopBar::resized()
{
    auto r = getLocalBounds();
    // the view tabs start over the panels, right of the sidebar column
    views.setBounds (kSideW + kPad, 0, views.idealWidth(), getHeight());

    // history, then the preset box with its arrows inside and its menu after,
    // at the same place for every category (the tabs it shows differ)
    const int iconY = r.getCentreY() - 12;
    undoButton.setBounds (views.getX() + ViewTabs::widestWidth() + sp::xl + sp::sm, iconY, 24, 24);
    redoButton.setBounds (undoButton.getRight() + sp::xs, iconY, 24, 24);
    presetArea = { redoButton.getRight() + sp::sm, r.getCentreY() - 16, 304, 32 };
    prev.setBounds (presetArea.getX() + sp::xs, iconY, 24, 24);
    next.setBounds (presetArea.getRight() - sp::xs - 24, iconY, 24, 24);
    menu.setBounds (presetArea.getRight() + sp::sm, iconY, 24, 24);
    save.setBounds (menu.getRight() + sp::xs, iconY, 24, 24);

    // right: the output window (as tall as the preset box), the patch's
    // level, then volume
    auto right = r.reduced (kPad, 0);
    const auto knob = [&] (ParamKnob& k, juce::Rectangle<int>& caption, const char* text) {
        k.setCompact (true);
        k.setBounds (right.removeFromRight (28).withSizeKeepingCentre (28, 28));
        right.removeFromRight (sp::sm);
        caption = right.removeFromRight (juce::GlyphArrangement::getStringWidthInt (label (10.f), text) + 2);
        right.removeFromRight (sp::lg);
    };
    knob (volume, volumeCaption, "VOL");
    knob (level, levelCaption, "LEVEL");
    right.removeFromLeft (save.getRight() - right.getX() + sp::xl);
    const int scopeW = std::min (232, right.getWidth());
    output.setBounds (right.removeFromRight (scopeW).withSizeKeepingCentre (scopeW, presetArea.getHeight()));
}

juce::Font TopBar::wordmarkFont()
{
    return juce::Font (juce::FontOptions (heading (15.f).getTypefacePtr()).withHeight (15.f).withKerningFactor (0.32f));
}

void TopBar::showPresetMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Init patch");
    m.addSeparator();
    const auto files = proc.listPresets();
    std::map<juce::String, juce::PopupMenu> byFolder;
    for (int i = 0; i < files.size(); ++i)
        byFolder[files[i].getParentDirectory().getFileName()].addItem (100 + i, files[i].getFileNameWithoutExtension(),
                                                                       true, files[i].getFileNameWithoutExtension() == proc.getPresetName());
    for (auto& [folder, sub] : byFolder)
        m.addSubMenu (folder, sub);
    if (files.isEmpty())
        m.addItem (2, "No saved presets yet", false);
    m.addSeparator();
    m.addItem (3, "Reveal preset folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menu), [this, files] (int res) {
        if (res == 1) proc.initPatch();
        else if (res == 3) PsyBoxProcessor::presetFolder().revealToUser();
        else if (res >= 100 && res - 100 < files.size()) proc.loadPreset (files[res - 100]);
    });
}

void TopBar::savePresetDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Name", juce::MessageBoxIconType::NoIcon, this);
    saveWindow->addTextEditor ("name", proc.getPresetName().trimCharactersAtEnd ("*"));
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    // The editor can close with the dialog open: JUCE then still calls this with 0, after ~TopBar.
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<TopBar> (this)] (int result) {
        if (safe == nullptr) return;
        if (result == 1 && safe->saveWindow != nullptr)
        {
            const auto name = safe->saveWindow->getTextEditorContents ("name").trim();
            if (name.isNotEmpty()) safe->saveNamed (name);
        }
        safe->saveWindow.reset();
    }), false);
}

// Asks before replacing a preset of the same name, and says so when the write fails.
void TopBar::saveNamed (const juce::String& name)
{
    const auto file = proc.presetFileFor (name);
    if (! file.existsAsFile())
    {
        writePreset (name);
        return;
    }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (
                                      juce::MessageBoxIconType::QuestionIcon, "Replace preset?",
                                      "A preset called \"" + file.getFileNameWithoutExtension() + "\" already exists in "
                                          + file.getParentDirectory().getFileName() + ".",
                                      "Replace", "Cancel", this),
                                  [safe = juce::Component::SafePointer<TopBar> (this), name] (int result) {
                                      if (safe != nullptr && result == 1) safe->writePreset (name);
                                  });
}

void TopBar::writePreset (const juce::String& name)
{
    if (proc.savePreset (name)) return;
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (
                                      juce::MessageBoxIconType::WarningIcon, "Preset not saved",
                                      "\"" + name + "\" could not be written to\n" + PsyBoxProcessor::presetFolder().getFullPathName()
                                          + "\nCheck that the folder exists and can be written to.",
                                      "OK", this),
                                  nullptr);
}
